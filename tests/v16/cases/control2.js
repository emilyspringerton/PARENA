var tot = 0;
for (const x of [1, 2, 3, 4]) { if (x == 3) continue; tot += x; }
console.log(tot);
for (let ch of "abc") console.log(ch);
var o = { a: 1, b: 2 };
for (var k in o) console.log(k, o[k]);
var n = 0;
do { n++; if (n == 2) continue; if (n > 4) break; } while (n < 10);
console.log(n);
function grade(s) {
  switch (s) {
    case 1: return "one";
    case 2:
    case 3: return "two-or-three";
    case "x": console.log("x!"); break;
    default: return "other";
  }
  return "after-switch";
}
console.log(grade(1), grade(2), grade(3), grade(9), grade("x"));
for (var i = 0; i < 3; i++) {
  switch (i) { case 0: console.log("zero"); break; case 1: console.log("one"); continue; default: console.log("d"); }
  console.log("end", i);
}
function f() { for (const v of [1, 2, 3]) { try { if (v == 2) return "ret" + v; } catch (e) { } } }
console.log(f());
var s = 0;
for (var i = 0; i < 3; i++) { try { if (i == 1) continue; s += i; } catch (e) { } }
console.log(s);
try { try { throw "inner"; } catch (e) { throw e + "-rethrown"; } } catch (e2) { console.log(e2); }
for (var i = 0; i < 2; i++) { for (const q of [10, 20]) { console.log(i, q); } }
