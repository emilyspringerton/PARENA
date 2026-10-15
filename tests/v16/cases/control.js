var n = 0;
if (n) console.log("t"); else console.log("f");
if (0) { console.log("a"); } else if (n === 0) { console.log("elseif"); } else { console.log("b"); }
var i = 0;
while (true) { i++; if (i > 6) break; if (i % 2 === 0) continue; console.log("odd", i); }
for (var j = 0; j < 3; j++) { for (var k = 0; k < 3; k++) { if (k == 1) continue; if (j == 2) break; console.log(j, k); } }
var t = 5 > 3 ? "big" : "small";
console.log(t, 1 < 2 ? 1 < 3 ? "a" : "b" : "c");
for (;;) { break; }
console.log("done");
