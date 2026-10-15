var s = "hello";
console.log(s.length, s.charAt(1), s[4], s[9], s.indexOf("ll"), s.indexOf("z"));
console.log(s.substring(1, 3), s.substring(2), "x" + 1 + 2, 1 + 2 + "x");
console.log('single' + "double", "tab\there", "quote\"inside", 'it\'s');
console.log("a" < "b", "abc" < "abd", "10" < "9", 10 < 9, "abc" == "abc");
var acc = "";
for (var i = 0; i < 5; i++) { acc = acc + i; }
console.log(acc, acc.length);
