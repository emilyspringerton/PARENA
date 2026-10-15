function counter() { var n = 0; return function () { n = n + 1; return n; }; }
var c1 = counter(), c2 = counter();
console.log(c1(), c1(), c1(), c2());
function adder(a) { return function (b) { return a + b; }; }
var add5 = adder(5);
console.log(add5(1), add5(10), adder(1)(2));
function compose(f, g) { return x => f(g(x)); }
var inc = x => x + 1, dbl = x => x * 2;
console.log(compose(inc, dbl)(10), compose(dbl, inc)(10));
function outer() { var a = 1; function mid() { var b = 2; return function inner() { return a + b + 3; }; } return mid(); }
console.log(outer()());
function shared() { var v = 0; return [function () { v++; return v; }, function () { return v; }]; }
var s = shared();
s[0](); s[0]();
console.log(s[1]());
var arrow0 = () => 42, arrow2 = (a, b) => { return a * b; };
console.log(arrow0(), arrow2(6, 7));
var fns = [];
for (var i = 0; i < 3; i++) { fns.push(() => i); }
console.log(fns[0](), fns[1](), fns[2]());
let top = 10; { let blk = 20; var g = () => top + blk; }
console.log(g());
function fibm() { var memo = {}; function f(n) { if (n < 2) return n; var k = "k" + n; if (memo[k] !== undefined) return memo[k]; var r = f(n - 1) + f(n - 2); memo[k] = r; return r; } return f; }
console.log(fibm()(50));
