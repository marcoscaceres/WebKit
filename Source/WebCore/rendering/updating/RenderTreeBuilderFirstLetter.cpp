/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2007 David Smith (catfish.man@gmail.com)
 * Copyright (C) 2003-2024 Apple Inc. All rights reserved.
 * Copyright (C) Research In Motion Limited 2010. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "RenderTreeBuilderFirstLetter.h"

#include "FontCascade.h"
#include "NodeInlines.h"
#include "RenderBlock.h"
#include "RenderButton.h"
#include "RenderInline.h"
#include "RenderSVGText.h"
#include "RenderStyleSetters.h"
#include "RenderTable.h"
#include "RenderTextFragment.h"
#include "RenderTreeBuilder.h"
#include "RenderView.h"
#include "StyleChange.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(RenderTreeBuilder::FirstLetter);

static std::optional<RenderStyle> styleForFirstLetter(const RenderElement& firstLetterContainer)
{
    auto& styleContainer = firstLetterContainer.isAnonymous() ? *firstLetterContainer.firstNonAnonymousAncestor() : firstLetterContainer;
    auto style = styleContainer.style().getCachedPseudoStyle({ PseudoId::FirstLetter });
    if (!style)
        return { };

    auto firstLetterStyle = RenderStyle::clone(*style);

    // If we have an initial letter drop that is >= 1, then we need to force floating to be on.
    if (firstLetterStyle.initialLetterDrop() >= 1 && !firstLetterStyle.isFloating())
        firstLetterStyle.setFloating(firstLetterStyle.writingMode().isBidiLTR() ? Float::Left : Float::Right);

    // We have to compute the correct font-size for the first-letter if it has an initial letter height set.
    auto* paragraph = firstLetterContainer.isRenderBlockFlow() ? &firstLetterContainer : firstLetterContainer.containingBlock();
    if (firstLetterStyle.initialLetterHeight() >= 1 && firstLetterStyle.metricsOfPrimaryFont().capHeight() && paragraph->style().metricsOfPrimaryFont().capHeight()) {
        // FIXME: For ideographic baselines, we want to go from line edge to line edge. This is equivalent to (N-1)*line-height + the font height.
        // We don't yet support ideographic baselines.
        // For an N-line first-letter and for alphabetic baselines, the cap-height of the first letter needs to equal (N-1)*line-height of paragraph lines + cap-height of the paragraph
        // Mathematically we can't rely on font-size, since font().height() doesn't necessarily match. For reliability, the best approach is simply to
        // compare the final measured cap-heights of the two fonts in order to get to the closest possible value.
        firstLetterStyle.setLineBoxContain({ Style::LineBoxContain::InitialLetter });
        int lineHeight = paragraph->style().computedLineHeight();

        // Set the font to be one line too big and then ratchet back to get to a precise fit. We can't just set the desired font size based off font height metrics
        // because many fonts bake ascent into the font metrics. Therefore we have to look at actual measured cap height values in order to know when we have a good fit.
        auto newFontDescription = firstLetterStyle.fontDescription();
        float capRatio = firstLetterStyle.metricsOfPrimaryFont().capHeight().value() / firstLetterStyle.computedFontSize();
        float startingFontSize = ((firstLetterStyle.initialLetterHeight() - 1) * lineHeight + paragraph->style().metricsOfPrimaryFont().intCapHeight()) / capRatio;
        newFontDescription.setSpecifiedSize(startingFontSize);
        newFontDescription.setComputedSize(startingFontSize);
        firstLetterStyle.setFontDescription(WTFMove(newFontDescription));

        int desiredCapHeight = (firstLetterStyle.initialLetterHeight() - 1) * lineHeight + paragraph->style().metricsOfPrimaryFont().intCapHeight();
        int actualCapHeight = firstLetterStyle.metricsOfPrimaryFont().intCapHeight();
        while (actualCapHeight > desiredCapHeight) {
            auto newFontDescription = firstLetterStyle.fontDescription();
            newFontDescription.setSpecifiedSize(newFontDescription.specifiedSize() - 1);
            newFontDescription.setComputedSize(newFontDescription.computedSize() -1);
            firstLetterStyle.setFontDescription(WTFMove(newFontDescription));
            actualCapHeight = firstLetterStyle.metricsOfPrimaryFont().intCapHeight();
        }
    }

    firstLetterStyle.setPseudoElementType(PseudoId::FirstLetter);
    // Force inline display (except for floating first-letters).
    firstLetterStyle.setDisplay(firstLetterStyle.isFloating() ? DisplayType::Block : DisplayType::Inline);
    // CSS2 says first-letter can't be positioned.
    firstLetterStyle.setPosition(PositionType::Static);

    return firstLetterStyle;
}

// CSS 2.1 http://www.w3.org/TR/CSS21/selector.html#first-letter
// "Punctuation (i.e, characters defined in Unicode [UNICODE] in the "open" (Ps), "close" (Pe),
// "initial" (Pi). "final" (Pf) and "other" (Po) punctuation classes), that precedes or follows the first letter should be included"
static inline bool isPunctuationForFirstLetter(char32_t c)
{
    return U_GET_GC_MASK(c) & (U_GC_PS_MASK | U_GC_PE_MASK | U_GC_PI_MASK | U_GC_PF_MASK | U_GC_PO_MASK);
}

static inline bool shouldSkipForFirstLetter(char32_t c)
{
    return deprecatedIsSpaceOrNewline(c) || c == noBreakSpace || isPunctuationForFirstLetter(c);
}

static bool supportsFirstLetter(RenderBlock& block)
{
    if (is<RenderButton>(block))
        return true;
    if (!is<RenderBlockFlow>(block))
        return false;
    if (is<RenderSVGText>(block))
        return false;
    return block.canHaveGeneratedChildren();
}

RenderTreeBuilder::FirstLetter::FirstLetter(RenderTreeBuilder& builder)
    : m_builder(builder)
{
}

void RenderTreeBuilder::FirstLetter::updateAfterDescendants(RenderBlock& block)
{
    if (!block.style().hasPseudoStyle(PseudoId::FirstLetter))
        return;

    if (!supportsFirstLetter(block))
        return;

    // FIXME: This should be refactored, firstLetterContainer is not needed.
    auto [firstLetter, firstLetterContainer] = block.firstLetterAndContainer();
    if (!firstLetter)
        return;

    // Other containers are handled when updating their renderers.
    if (&block != firstLetterContainer)
        return;

    // If the child already has style, then it has already been created, so we just want
    // to update it.
    if (firstLetter->parent()->style().pseudoElementType() == PseudoId::FirstLetter) {
        updateStyle(block, *firstLetter);
        return;
    }

    if (!is<RenderText>(firstLetter))
        return;

    createRenderers(downcast<RenderText>(*firstLetter));
}

void RenderTreeBuilder::FirstLetter::cleanupOnDestroy(RenderTextFragment& textFragment)
{
    if (!textFragment.firstLetter())
        return;
    m_builder.destroy(*textFragment.firstLetter(), CanCollapseAnonymousBlock::No);
}

void RenderTreeBuilder::FirstLetter::updateStyle(RenderBlock& firstLetterBlock, RenderObject& currentChild)
{
    RenderElement* firstLetter = currentChild.parent();
    ASSERT(firstLetter->isFirstLetter());
    ASSERT(firstLetter->isFloating() || firstLetter->isInline());
    if (!firstLetter || !firstLetter->parent())
        return;

    auto& firstLetterContainer = *firstLetter->parent();
    auto pseudoStyle = styleForFirstLetter(firstLetterContainer);
    if (!pseudoStyle) {
        ASSERT_NOT_REACHED();
        return;
    }

    if (!Style::determineChanges(firstLetter->style(), *pseudoStyle).contains(Style::Change::Renderer)) {
        firstLetter->setStyle(WTFMove(*pseudoStyle));
        return;
    }

    // The first-letter renderer needs to be replaced. Create a new renderer of the right type.
    RenderPtr<RenderBoxModelObject> newFirstLetter;
    if (pseudoStyle->display() == DisplayType::Inline)
        newFirstLetter = createRenderer<RenderInline>(RenderObject::Type::Inline, firstLetterBlock.document(), WTFMove(*pseudoStyle));
    else
        newFirstLetter = createRenderer<RenderBlockFlow>(RenderObject::Type::BlockFlow, firstLetterBlock.document(), WTFMove(*pseudoStyle));
    newFirstLetter->initializeStyle();
    newFirstLetter->setIsFirstLetter();

    // Move the first letter into the new renderer.
    while (RenderObject* child = firstLetter->firstChild()) {
        auto toMove = m_builder.detach(*firstLetter, *child, WillBeDestroyed::No);
        m_builder.attach(*newFirstLetter, WTFMove(toMove));
    }

    WeakPtr remainingText = downcast<RenderBoxModelObject>(*firstLetter).firstLetterRemainingText();
    ASSERT(!remainingText || remainingText->isAnonymous() || remainingText->textNode()->renderer() == remainingText);
    WeakPtr nextSibling = firstLetter->nextSibling();
    m_builder.destroy(*firstLetter);
    if (remainingText) {
        // Replace the old renderer with the new one.
        remainingText->setFirstLetter(*newFirstLetter);
        newFirstLetter->setFirstLetterRemainingText(*remainingText);
    }
    m_builder.attach(firstLetterContainer, WTFMove(newFirstLetter), nextSibling.get());
}

void RenderTreeBuilder::FirstLetter::createRenderers(RenderText& currentTextChild)
{
    RenderElement* textContentParent = currentTextChild.parent();
    RenderElement* firstLetterContainer = nullptr;
    if (auto* wrapperInlineForDisplayContents = currentTextChild.inlineWrapperForDisplayContents())
        firstLetterContainer = wrapperInlineForDisplayContents->parent();
    else
        firstLetterContainer = textContentParent;
    if (!firstLetterContainer)
        return;
    
    auto pseudoStyle = styleForFirstLetter(*firstLetterContainer);
    if (!pseudoStyle)
        return;

    RenderPtr<RenderBoxModelObject> newFirstLetter;
    if (pseudoStyle->display() == DisplayType::Inline)
        newFirstLetter = createRenderer<RenderInline>(RenderObject::Type::Inline, currentTextChild.document(), WTFMove(*pseudoStyle));
    else
        newFirstLetter = createRenderer<RenderBlockFlow>(RenderObject::Type::BlockFlow, currentTextChild.document(), WTFMove(*pseudoStyle));
    newFirstLetter->initializeStyle();
    newFirstLetter->setIsFirstLetter();

    // The original string is going to be either a generated content string or a DOM node's
    // string. We want the original string before it got transformed in case first-letter has
    // no text-transform or a different text-transform applied to it.
    String oldText = currentTextChild.originalText();
    ASSERT(!oldText.isNull());

    if (!oldText.isEmpty()) {
        unsigned length = 0;

        // Account for leading spaces and punctuation.
        while (length < oldText.length() && shouldSkipForFirstLetter(oldText.characterStartingAt(length)))
            length += numCodeUnitsInGraphemeClusters(StringView(oldText).substring(length), 1);

        // Account for first grapheme cluster.
        length += numCodeUnitsInGraphemeClusters(StringView(oldText).substring(length), 1);

        // Keep looking for whitespace and allowed punctuation, but avoid
        // accumulating just whitespace into the :first-letter.
        unsigned numCodeUnits = 0;
        for (unsigned scanLength = length; scanLength < oldText.length(); scanLength += numCodeUnits) {
            char32_t c = oldText.characterStartingAt(scanLength);

            if (!shouldSkipForFirstLetter(c))
                break;

            numCodeUnits = numCodeUnitsInGraphemeClusters(StringView(oldText).substring(scanLength), 1);

            if (isPunctuationForFirstLetter(c))
                length = scanLength + numCodeUnits;
        }

        auto* textNode = currentTextChild.textNode();
        WeakPtr beforeChild = currentTextChild.nextSibling();
        WeakPtr inlineWrapperForDisplayContents = currentTextChild.inlineWrapperForDisplayContents();
        auto hasInlineWrapperForDisplayContents = inlineWrapperForDisplayContents.get();
        m_builder.destroy(currentTextChild);

        // Construct a text fragment for the text after the first letter.
        // This text fragment might be empty.
        RenderPtr<RenderTextFragment> newRemainingText;
        if (textNode) {
            newRemainingText = createRenderer<RenderTextFragment>(*textNode, oldText, length, oldText.length() - length);
            textNode->setRenderer(newRemainingText.get());
        } else
            newRemainingText = createRenderer<RenderTextFragment>(m_builder.m_view.document(), oldText, length, oldText.length() - length);

        RenderTextFragment& remainingText = *newRemainingText;
        ASSERT_UNUSED(hasInlineWrapperForDisplayContents, hasInlineWrapperForDisplayContents == inlineWrapperForDisplayContents.get());
        remainingText.setInlineWrapperForDisplayContents(inlineWrapperForDisplayContents.get());
        m_builder.attach(*textContentParent, WTFMove(newRemainingText), beforeChild.get());

        // FIXME: Make attach the final step so that we don't need to keep firstLetter around.
        auto& firstLetter = *newFirstLetter;
        remainingText.setFirstLetter(firstLetter);
        firstLetter.setFirstLetterRemainingText(remainingText);
        m_builder.attach(*firstLetterContainer, WTFMove(newFirstLetter), &remainingText);

        // Construct text fragment for the first letter.
        auto letter = createRenderer<RenderTextFragment>(m_builder.m_view.document(), oldText, 0, length);
        m_builder.attach(firstLetter, WTFMove(letter));
    }
}

};
