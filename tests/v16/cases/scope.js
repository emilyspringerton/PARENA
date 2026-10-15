let r = 0;
{ let r = 5; console.log("inner", r); }
console.log("outer", r);
function f() { for (var i = 0; i < 3; i++) { var j = i * 2; } return i + j; }
console.log(f());
function g() { let t = 0; for (let i = 0; i < 3; i++) { let k = i; t += k; } { let t = 100; } return t; }
console.log(g());
const c = 10;
var v = 1; var v = 2;
console.log(c, v, typeof undeclared);
function shadow(x) { var x = x + 1; return x; }
console.log(shadow(1));
