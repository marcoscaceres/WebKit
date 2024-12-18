/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "AnimationEffectTiming.h"

#include "WebAnimationUtilities.h"

namespace WebCore {

void AnimationEffectTiming::updateComputedProperties(IsProgressBased isProgressBased)
{
    // https://drafts.csswg.org/web-animations-2/#intrinsic-iteration-duration
    if (isProgressBased == IsProgressBased::Yes && iterations)
        intrinsicIterationDuration = WebAnimationTime::fromPercentage(100) / iterations;
    else
        intrinsicIterationDuration = iterationDuration;

    // 3.8.2. Calculating the active duration
    // https://drafts.csswg.org/web-animations-1/#calculating-the-active-duration

    // The active duration is calculated as follows:
    // active duration = iteration duration × iteration count
    // If either the iteration duration or iteration count are zero, the active duration is zero.
    if (intrinsicIterationDuration.isZero() || !iterations)
        activeDuration = intrinsicIterationDuration.matchingZero();
    else
        activeDuration = intrinsicIterationDuration * iterations;

    // 3.5.3 The active interval
    // https://drafts.csswg.org/web-animations-1/#end-time
    if (intrinsicIterationDuration.percentage())
        endTime = activeDuration;
    else {
        // The end time of an animation effect is the result of evaluating max(start delay + active duration + end delay, 0).
        endTime = delay + activeDuration + endDelay;
        if (endTime < 0_s)
            endTime = 0_s;
    }
}

BasicEffectTiming AnimationEffectTiming::getBasicTiming(const ResolutionData& data) const
{
    // The Web Animations spec introduces a number of animation effect time-related definitions that refer
    // to each other a fair bit, so rather than implementing them as individual methods, it's more efficient
    // to return them all as a single BasicEffectTiming.

    auto localTime = data.localTime;

    auto phase = [this, data, localTime]() -> AnimationEffectPhase {
        // 3.5.5. Animation effect phases and states
        // https://drafts.csswg.org/web-animations-2/#animation-effect-phases-and-states

        // (This should be the last statement, but it's more efficient to cache the local time and return right away if it's not resolved.)
        // Furthermore, it is often convenient to refer to the case when an animation effect is in none of the above phases
        // as being in the idle phase.
        if (!localTime)
            return AnimationEffectPhase::Idle;

        auto atProgressTimelineBoundary = [&]() {
            // https://drafts.csswg.org/web-animations-2/#at-progress-timeline-boundary
            // If any of the following conditions are true:
            // - the associated animation's timeline is not a progress-based timeline, or
            // - the associated animation's timeline duration is unresolved or zero, or
            // - the animation’s playback rate is zero
            // return false
            if (!data.timelineDuration || data.timelineDuration->isZero())
                return false;
            if (!data.playbackRate)
                return false;
            // Let effective start time be the animation’s start time if resolved, or zero otherwise.
            auto effectiveStartTime = data.startTime.value_or(WebAnimationTime::fromPercentage(0));
            // Set unlimited current time based on the first matching condition:
            // - start time is resolved: (timeline time - start time) × playback rate
            // - Otherwise: animation's current time
            ASSERT_IMPLIES(data.startTime, data.timelineTime);
            auto unlimitedCurrentTime = data.startTime ? (*data.timelineTime - *data.startTime) * data.playbackRate : *data.localTime;
            // Let effective timeline time be unlimited current time / animation’s playback rate + effective start time
            auto effectiveTimelineTime = unlimitedCurrentTime / data.playbackRate + effectiveStartTime;
            // Let effective timeline progress be effective timeline time / timeline duration
            auto effectiveTimelineProgress = effectiveTimelineTime / *data.timelineDuration;
            // If effective timeline progress is 0 or 1, return true, otherwise false.
            return !effectiveTimelineProgress || effectiveTimelineProgress == 1;
        };

        auto animationIsBackwards = data.playbackRate < 0;

        auto beforeActiveBoundaryTime = [&]() -> WebAnimationTime {
            if (auto endTimeSeconds = endTime.time())
                return { std::max(std::min(delay, *endTimeSeconds), 0_s) };
            return endTime.matchingZero();
        }();

        // An animation effect is in the before phase if the animation effect's local time is not unresolved and
        // either of the following conditions are met:
        //     1. the local time is less than the before-active boundary time, or
        //     2. the animation direction is "backwards" and the local time is equal to the before-active boundary time
        //        and not at progress timeline boundary.
        if (localTime->approximatelyLessThan(beforeActiveBoundaryTime) || (animationIsBackwards && localTime->approximatelyEqualTo(beforeActiveBoundaryTime) && !atProgressTimelineBoundary()))
            return AnimationEffectPhase::Before;

        auto activeAfterBoundaryTime = [&]() -> WebAnimationTime {
            if (endTime.percentage())
                return std::max(std::min(activeDuration, endTime), activeDuration.matchingZero());
            ASSERT(endTime.time());
            ASSERT(activeDuration.time());
            return { std::max(std::min(delay + *activeDuration.time(), *endTime.time()), 0_s) };
        }();

        // An animation effect is in the after phase if the animation effect's local time is not unresolved
        // and either of the following conditions are met:
        //     1. the local time is greater than the active-after boundary time, or
        //     2. the animation direction is "forwards" and the local time is equal to the active-after boundary time
        //        and not at progress timeline boundary.
        if (localTime->approximatelyGreaterThan(activeAfterBoundaryTime) || (!animationIsBackwards && localTime->approximatelyEqualTo(activeAfterBoundaryTime) && !atProgressTimelineBoundary()))
            return AnimationEffectPhase::After;

        // An animation effect is in the active phase if the animation effect’s local time is not unresolved and it is not
        // in either the before phase nor the after phase.
        // (No need to check, we've already established that local time was resolved).
        return AnimationEffectPhase::Active;
    }();

    auto activeTime = [this, localTime, phase]() -> std::optional<WebAnimationTime> {
        // 3.8.3.1. Calculating the active time
        // https://drafts.csswg.org/web-animations-1/#calculating-the-active-time

        // The active time is based on the local time and start delay. However, it is only defined
        // when the animation effect should produce an output and hence depends on its fill mode
        // and phase as follows,

        // If the animation effect is in the before phase, the result depends on the first matching
        // condition from the following,
        if (phase == AnimationEffectPhase::Before) {
            // If the fill mode is backwards or both, return the result of evaluating
            // max(local time - start delay, 0).
            if (fill == FillMode::Backwards || fill == FillMode::Both) {
                if (auto percentage = localTime->percentage())
                    return std::max(*localTime, localTime->matchingZero());
                return std::max(*localTime->time() - delay, 0_s);
            }
            // Otherwise, return an unresolved time value.
            return std::nullopt;
        }

        // If the animation effect is in the active phase, return the result of evaluating local time - start delay.
        if (phase == AnimationEffectPhase::Active) {
            if (localTime->percentage())
                return *localTime;
            return *localTime - delay;
        }

        // If the animation effect is in the after phase, the result depends on the first matching
        // condition from the following,
        if (phase == AnimationEffectPhase::After) {
            // If the fill mode is forwards or both, return the result of evaluating
            // max(min(local time - start delay, active duration), 0).
            if (fill == FillMode::Forwards || fill == FillMode::Both) {
                if (localTime->percentage())
                    return std::max(std::min(*localTime, activeDuration), activeDuration.matchingZero());
                return std::max(std::min(*localTime->time() - delay, *activeDuration.time()), 0_s);
            }
            // Otherwise, return an unresolved time value.
            return std::nullopt;
        }

        // Otherwise (the local time is unresolved), return an unresolved time value.
        return std::nullopt;
    }();

    return { localTime, activeTime, endTime, activeDuration, phase };
}

enum ComputedDirection : uint8_t { Forwards, Reverse };

ResolvedEffectTiming AnimationEffectTiming::resolve(const ResolutionData& data) const
{
    // The Web Animations spec introduces a number of animation effect time-related definitions that refer
    // to each other a fair bit, so rather than implementing them as individual methods, it's more efficient
    // to return them all as a single ComputedEffectTiming.

    auto basicEffectTiming = getBasicTiming(data);
    auto activeTime = basicEffectTiming.activeTime;
    auto phase = basicEffectTiming.phase;

    auto overallProgress = [this, phase, activeTime]() -> std::optional<double> {
        // 3.8.3.2. Calculating the overall progress
        // https://drafts.csswg.org/web-animations-1/#calculating-the-overall-progress

        // The overall progress describes the number of iterations that have completed (including partial iterations) and is defined as follows:

        // 1. If the active time is unresolved, return unresolved.
        if (!activeTime)
            return std::nullopt;

        // 2. Calculate an initial value for overall progress based on the first matching condition from below,
        auto overallProgress = [&]() {
            // If the iteration duration is zero, if the animation effect is in the before phase, let overall progress be zero,
            // otherwise, let it be equal to the iteration count.
            if (intrinsicIterationDuration.isZero())
                return phase == AnimationEffectPhase::Before ? 0 : iterations;
            // Otherwise, let overall progress be the result of calculating active time / iteration duration.
            return *activeTime / intrinsicIterationDuration;
        }();

        // 3. Return the result of calculating overall progress + iteration start.
        overallProgress += iterationStart;
        return std::abs(overallProgress);
    }();

    auto simpleIterationProgress = [this, overallProgress, phase, activeTime]() -> std::optional<double> {
        // 3.8.3.3. Calculating the simple iteration progress
        // https://drafts.csswg.org/web-animations-1/#calculating-the-simple-iteration-progress

        // The simple iteration progress is a fraction of the progress through the current iteration that
        // ignores transformations to the time introduced by the playback direction or timing functions
        // applied to the effect, and is calculated as follows:

        // 1. If the overall progress is unresolved, return unresolved.
        if (!overallProgress)
            return std::nullopt;

        // 2. If overall progress is infinity, let the simple iteration progress be iteration start % 1.0,
        // otherwise, let the simple iteration progress be overall progress % 1.0.
        double simpleIterationProgress = std::isinf(*overallProgress) ? fmod(iterationStart, 1) : fmod(*overallProgress, 1);

        // 3. If all of the following conditions are true,
        //
        // the simple iteration progress calculated above is zero, and
        // the animation effect is in the active phase or the after phase, and
        // the active time is equal to the active duration, and
        // the iteration count is not equal to zero.
        // let the simple iteration progress be 1.0.
        if (!simpleIterationProgress && (phase == AnimationEffectPhase::Active || phase == AnimationEffectPhase::After) && activeTime->approximatelyEqualTo(activeDuration) && iterations)
            return 1;

        return simpleIterationProgress;
    }();

    auto currentIteration = [this, activeTime, phase, simpleIterationProgress, overallProgress]() -> std::optional<double> {
        // 3.8.4. Calculating the current iteration
        // https://drafts.csswg.org/web-animations-1/#calculating-the-current-iteration

        // The current iteration can be calculated using the following steps:

        // 1. If the active time is unresolved, return unresolved.
        if (!activeTime)
            return std::nullopt;

        // 2. If the animation effect is in the after phase and the iteration count is infinity, return infinity.
        if (phase == AnimationEffectPhase::After && std::isinf(iterations))
            return std::numeric_limits<double>::infinity();

        // 3. If the simple iteration progress is 1.0, return floor(overall progress) - 1.
        if (*simpleIterationProgress == 1)
            return floor(*overallProgress) - 1;

        // 4. Otherwise, return floor(overall progress).
        return floor(*overallProgress);
    }();

    auto currentDirection = [this, currentIteration]() -> ComputedDirection {
        // 3.9.1. Calculating the directed progress
        // https://drafts.csswg.org/web-animations-1/#calculating-the-directed-progress

        // If playback direction is normal, let the current direction be forwards.
        if (direction == PlaybackDirection::Normal)
            return ComputedDirection::Forwards;

        // If playback direction is reverse, let the current direction be reverse.
        if (direction == PlaybackDirection::Reverse)
            return ComputedDirection::Reverse;

        if (!currentIteration)
            return ComputedDirection::Forwards;

        // Otherwise, let d be the current iteration.
        auto d = *currentIteration;
        // If playback direction is alternate-reverse increment d by 1.
        if (direction == PlaybackDirection::AlternateReverse)
            d++;
        // If d % 2 == 0, let the current direction be forwards, otherwise let the current direction be reverse.
        // If d is infinity, let the current direction be forwards.
        if (std::isinf(d) || !fmod(d, 2))
            return ComputedDirection::Forwards;
        return ComputedDirection::Reverse;
    }();

    auto directedProgress = [simpleIterationProgress, currentDirection]() -> std::optional<double> {
        // 3.9.1. Calculating the directed progress
        // https://drafts.csswg.org/web-animations-1/#calculating-the-directed-progress

        // The directed progress is calculated from the simple iteration progress using the following steps:

        // 1. If the simple iteration progress is unresolved, return unresolved.
        if (!simpleIterationProgress)
            return std::nullopt;

        // 2. Calculate the current direction (we implement this as a separate method).

        // 3. If the current direction is forwards then return the simple iteration progress.
        if (currentDirection == ComputedDirection::Forwards)
            return *simpleIterationProgress;

        // Otherwise, return 1.0 - simple iteration progress.
        return 1 - *simpleIterationProgress;
    }();

    auto [transformedProgress, before] = [this, directedProgress, currentDirection, phase]() -> std::pair<std::optional<double>, TimingFunction::Before> {
        // 3.10.1. Calculating the transformed progress
        // https://drafts.csswg.org/web-animations-1/#calculating-the-transformed-progress
        auto before = TimingFunction::Before::No;

        // The transformed progress is calculated from the directed progress using the following steps:
        //
        // 1. If the directed progress is unresolved, return unresolved.
        if (!directedProgress)
            return { std::nullopt, before };

        if (!intrinsicIterationDuration.isZero()) {
            // 2. Calculate the value of the before flag as follows:
            // 1. Determine the current direction using the procedure defined in §3.9.1 Calculating the directed progress.
            // 2. If the current direction is forwards, let going forwards be true, otherwise it is false.
            bool goingForwards = currentDirection == ComputedDirection::Forwards;
            // 3. The before flag is set if the animation effect is in the before phase and going forwards is true;
            //    or if the animation effect is in the after phase and going forwards is false.
            if ((phase == AnimationEffectPhase::Before && goingForwards) || (phase == AnimationEffectPhase::After && !goingForwards))
                before = TimingFunction::Before::Yes;

            // 3. Return the result of evaluating the animation effect’s timing function passing directed progress as the
            //    input progress value and before flag as the before flag.
            auto transformProgressDuration = [&]() {
                if (auto time = intrinsicIterationDuration.time())
                    return time->seconds();
                return 1.0;
            };

            return { timingFunction->transformProgress(*directedProgress, transformProgressDuration(), before), before };
        }

        return { *directedProgress, before };
    }();

    return { currentIteration, phase, transformedProgress, simpleIterationProgress, before };
}

} // namespace WebCore
