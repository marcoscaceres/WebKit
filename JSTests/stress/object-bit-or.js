function foo(a, b) {
    return a | b;
}

noInline(foo);

var things = [{valueOf: function() { return 6; }}];
var results = [14];

for (var i = 0; i < testLoopCount; ++i) {
    var result = foo(things[i % things.length], 10);
    var expected = results[i % results.length];
    if (result != expected)
        throw "Error: bad result for i = " + i + ": " + result;
}

