var a = [5, 3, 8, 1];
a.forEach(function (x, i) { console.log(i, x); });
console.log(a.map(x => x * 2), a.filter(x => x > 2), a.reduce((s, x) => s + x, 0), a.reduce((s, x) => s + x));
console.log(a.some(x => x > 7), a.every(x => x > 7), a.find(x => x > 4), a.findIndex(x => x > 4), a.find(x => x > 100));
console.log(a.indexOf(8), a.indexOf(9), a.includes(3), a.slice(1, 3), a.slice(-2), a.concat([9, 10], 11));
console.log(a.sort(), a.sort((x, y) => y - x), a.reverse(), a.shift(), a);
console.log([10, 9, 1].sort(), ["b", "a", "c"].sort());
console.log("Hello World".toUpperCase(), "Hello".toLowerCase(), "  pad ".trim() + "|", "a,b,c".split(","), "abc".split(""), "xyz".slice(1), "xyz".slice(-2));
console.log("abc".includes("bc"), "abc".startsWith("ab"), "ab".repeat(3), "A".charCodeAt(0));
console.log(parseInt("42px"), parseInt("0x1f"), parseInt("ff", 16), parseInt("abc"), parseFloat("3.14abc"), Number("12"), Number(""), Number("x"), String(12), isNaN("abc"), Boolean(""), Boolean("a"));
console.log(Object.keys({ a: 1, b: 2 }), Array.isArray([]), Array.isArray({}), JSON.stringify({ a: [1, "two", null, true], b: { c: "q\"t" } }));
console.log(Math.round(2.5), Math.round(-2.5), Math.trunc(-3.7), Math.PI);
try { null.x; } catch (e) { console.log("caught", e.name, e.message); }
try { undefinedFn(); } catch (e) { console.log(e.name + ": " + e.message); }
try { throw new Error("boom"); } catch (e) { console.log(e.message, e.name); }
try { throw "plain"; } catch (e) { console.log(e); }
function thrower() { throw new TypeError("deep"); }
function mid() { thrower(); }
try { mid(); } catch (e) { console.log("from", e.name, e.message); }
try { [1, 2].forEach(function (x) { if (x == 2) throw new RangeError("in callback"); }); } catch (e) { console.log(e.name, e.message); }
var r = [1, 2, 3].map(function (x) { try { if (x == 2) throw "two"; return x; } catch (e) { return e; } });
console.log(r);
console.log("after");
throw new Error("uncaught!");
