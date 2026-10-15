// sieve
function sieve(n) { var is = []; for (var i = 0; i <= n; i++) is.push(true); is[0] = is[1] = false;
  for (var p = 2; p * p <= n; p++) if (is[p]) for (var m = p * p; m <= n; m += p) is[m] = false;
  var out = []; for (var k = 0; k <= n; k++) if (is[k]) out.push(k); return out; }
console.log(sieve(50).join(" "));
// hanoi
var moves = 0; function hanoi(n, a, b, c) { if (n == 0) return; hanoi(n - 1, a, c, b); moves++; hanoi(n - 1, c, b, a); }
hanoi(10, "A", "B", "C"); console.log(moves);
// matrix multiply
function matmul(A, B) { var n = A.length, C = []; for (var i = 0; i < n; i++) { C.push([]); for (var j = 0; j < n; j++) { var s = 0; for (var k = 0; k < n; k++) s += A[i][k] * B[k][j]; C[i].push(s); } } return C; }
console.log(matmul([[1, 2], [3, 4]], [[5, 6], [7, 8]]));
// word frequency
var text = "the quick brown fox jumps over the lazy dog the end";
var freq = {}; text.split(" ").forEach(function (w) { freq[w] = (freq[w] || 0) + 1; });
console.log(freq, Object.keys(freq).length);
// memoize with closure
function memo(f) { var cache = {}; return function (n) { if (cache[n] === undefined) cache[n] = f(n); return cache[n]; }; }
var slow = memo(function (n) { return n * n; });
console.log(slow(12), slow(12), slow(3));
// bubble sort
var arr = [64, 34, 25, 12, 22, 11, 90];
for (var i = 0; i < arr.length; i++) for (var j = 0; j < arr.length - i - 1; j++) if (arr[j] > arr[j + 1]) { var t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t; }
console.log(arr.join(","));
// string building / reverse / palindrome
function rev(s) { var r = ""; for (var i = s.length - 1; i >= 0; i--) r += s[i]; return r; }
console.log(rev("racecar") === "racecar", rev("hello"));
// gcd, collatz
function gcd(a, b) { return b == 0 ? a : gcd(b, a % b); }
console.log(gcd(1071, 462));
var steps = 0, n = 27; while (n != 1) { n = n % 2 == 0 ? n / 2 : 3 * n + 1; steps++; } console.log(steps);
// JSON roundtrip-ish
console.log(JSON.stringify([1, { a: [true, null] }, "s"]));
// reduce/map/filter chain
console.log([1,2,3,4,5,6].filter(x => x % 2 == 0).map(x => x * x).reduce((a, b) => a + b, 0));
// nested closures counter generator
function gen() { var i = 0; return { next: function () { i = i + 1; return i; }, reset: function () { i = 0; } }; }
var g = gen(); g.next(); g.next(); console.log(g.next()); g.reset(); console.log(g.next());
// typeof / ternary chains
console.log([1, "a", true, null, undefined, [], {}, function(){}].map(x => typeof x).join());
// number formatting
console.log(0.1 * 3, 100 / 3, 1e300 * 10, 2 ** 3 ** 2, (-2) ** 2);
