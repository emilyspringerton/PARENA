function fib(n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
console.log(fib(20));
function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); }
console.log(fact(10), fact(20));
function mk(a, b) { var c = a * b; return c + 1; }
console.log(mk(3, 4), mk(2), mk(1, 2, 3));
function noret() { var x = 1; }
console.log(noret(), typeof noret);
function later() { return helper() + 1; }
function helper() { return 41; }
console.log(later());
var g = 0;
function inc() { g = g + 1; return g; }
inc(); inc();
console.log(inc(), g);
var anon = function (x) { return x * 2; };
console.log(anon(21));
function isEven(n) { return n === 0 ? true : isOdd(n - 1); }
function isOdd(n) { return n === 0 ? false : isEven(n - 1); }
console.log(isEven(10), isOdd(7));
