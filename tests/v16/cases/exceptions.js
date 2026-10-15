function risky(n) { if (n > 2) throw new RangeError("too big: " + n); return n; }
function safe(n) { try { return risky(n); } catch (e) { return e.name + "/" + e.message; } }
console.log(safe(1), safe(5));
var log = [];
try { log.push("a"); null.boom; log.push("b"); } catch (e) { log.push(e.name); }
console.log(log);
try { notDefinedAnywhere(); } catch (e) { console.log(e.name, e.message); }
try { var q = 1; q(); } catch (e) { console.log(e.name); }
try { throw { code: 7 }; } catch (e) { console.log(e.code); }
function deep(n) { if (n == 0) throw new Error("bottom"); return deep(n - 1); }
try { deep(50); } catch (e) { console.log(e.message); }
var out = [3, 4].map(function (x) { try { if (x == 4) throw new Error("four"); return x; } catch (e) { return e.message; } });
console.log(out);
function f() { try { return "tried"; } catch (e) { return "caught"; } }
console.log(f(), f());
console.log("before uncaught");
throw new TypeError("final");
