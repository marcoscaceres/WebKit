// Copyright (C) 2015 the V8 project authors. All rights reserved.
// Copyright (C) 2025 Jonas Haukenes. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: proposal-upsert
description: |
  WeakMap.prototype.getOrInsertComputed.length descriptor
info: |
  WeakMap.prototype.getOrInsertComputed ( key, callbackfn )

  17 ECMAScript Standard Built-in Objects
includes: [propertyHelper.js]
features: [upsert]
flags: [noStrict]
---*/
verifyProperty(WeakMap.prototype.getOrInsertComputed, "length", {
  value: 2,
  writable: false,
  enumerable: false,
  configurable: true
});

