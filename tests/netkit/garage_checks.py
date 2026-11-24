"""The garage's checks, independent of how nk_garage.prn is executed. run(call, cstr, check): call(name, *i32) -> int, cstr(addr) -> str."""
def run(call, cstr, check):
    g = lambda n, *a: call("nk_garage_" + n, *a)
    L, P = (1280, 800), (800, 1280)                      # landscape tablet, portrait phone (view size, px)
    check(g("count") == 4, "four objects: scanner bench, terminal, monitor, arcade cabinet")
    check(g("cols", *L) == 4 and g("rows", *L) == 1, "landscape: one row of four")
    check(g("cols", *P) == 2 and g("rows", *P) == 2, "portrait: a 2 x 2 grid")
    for name, V in (("landscape", L), ("portrait", P)):
        boxes = {i: (g("obj_x", i, *V), g("obj_y", i, *V), g("obj_w", i, *V), g("obj_h", i, *V)) for i in range(1, 5)}
        check(all(w > 0 and h > 0 and x >= 0 and y >= 0 and x + w <= 1000 and y + h <= 1000 for x, y, w, h in boxes.values()),
              f"{name}: every object is inside the floor and has a size")
        def overlap(a, b): return a[0] < b[0] + b[2] and b[0] < a[0] + a[2] and a[1] < b[1] + b[3] and b[1] < a[1] + a[3]
        check(not any(overlap(boxes[i], boxes[j]) for i in boxes for j in boxes if i < j), f"{name}: no two objects overlap")
        check(all(g("hit", x + w // 2, y + h // 2, *V) == i for i, (x, y, w, h) in boxes.items()), f"{name}: tapping an object's middle hits that object")
        check(all(g("hit", x + 1, y + 1, *V) == i for i, (x, y, w, h) in boxes.items()), f"{name}: tapping an object's corner hits it")
        check(all(g("hit", x - 5 if x >= 5 else x, y, *V) in (i, i - 1) for i, (x, y, w, h) in boxes.items()), f"{name}: the margin goes to a neighbour, never to nothing")
        check(g("hit", 1000, 500, *V) == 0 and g("hit", -1, 500, *V) == 0 and g("hit", 500, 1000, *V) == 0, f"{name}: a tap outside the floor hits nothing")
        check(g("anchor_x", 2, *V) == boxes[2][0] + boxes[2][2] // 2, f"{name}: a cable attaches at an object's middle")
    check(g("obj_w", 0, *L) == 0 and g("obj_w", 5, *L) == 0, "ids 0 and 5 are not objects")
    # lights are true to the state they are given
    check(g("light", 1, 0, 0, 0) == 0, "bench: never scanned -> off")
    check(g("light", 1, 1, 0, 0) == 2 and g("light", 1, 1, 7, 0) == 2, "bench: scanning -> amber")
    check(g("light", 1, 2, 3, 0) == 1, "bench: scanned, devices found -> green")
    check(g("light", 1, 2, 0, 0) == 3, "bench: scanned, nothing found -> red")
    for i in (2, 3):
        check([g("light", i, s, 0, 0) for s in (0, 1, 2)] == [0, 2, 1], f"object {i}: none off, connecting amber, live green")
    check([g("light", 4, s, 0, 0) for s in (0, 1, 2, 3)] == [0, 2, 1, 3], "cabinet: unloaded off, loading amber, ready green, failed red")
    check(g("light", 9, 2, 2, 2) == 0, "an unknown object has no light")
    check([cstr(g("light_name_addr", l)) for l in range(4)] == ["off", "green", "amber", "red"], "light names")
    # words
    check([cstr(g("label_addr", i)) for i in range(1, 5)] == ["Scanner bench", "Terminal", "Monitor", "Arcade cabinet"], "labels")
    check(all(cstr(g("sub_addr", i)) != "" for i in range(1, 5)), "every object says what it is for")
    check(cstr(g("status_addr", 1, 2)) == "Scanning" and cstr(g("status_addr", 1, 3)) == "Nothing found", "bench status words follow the light")
    check(cstr(g("status_addr", 4, 3)) == "Could not load" and cstr(g("status_addr", 4, 0)) == "Tap to load", "cabinet status words follow the light")
    check(all(cstr(g("status_addr", i, l)) != "" for i in range(1, 5) for l in range(4)), "no object/light pair is left without words")
    # cables
    check(g("cable_count") == 2, "two possible cables")
    check([(g("cable_from", i), g("cable_to", i)) for i in (0, 1)] == [(1, 2), (1, 3)], "cables run bench-terminal and bench-monitor")
    check([g("cable_live", 0, s, 0) for s in (0, 1, 2)] == [0, 0, 1], "SSH cable exists only while the session is live")
    check([g("cable_live", 1, 0, s) for s in (0, 1, 2)] == [0, 0, 1], "VNC cable exists only while the session is live")
    check(g("cable_live", 0, 0, 2) == 0 and g("cable_live", 1, 2, 0) == 0, "one session never lights the other's cable")
    # taps
    check([g("action", i, 0, 0) for i in (1, 2, 3, 4)] == [1, 5, 6, 4], "with no sessions: bench->devices, terminal/monitor->devices with a hint, cabinet->arcade")
    check(g("action", 2, 2, 0) == 2 and g("action", 3, 0, 2) == 3 and g("action", 2, 1, 0) == 2, "terminal and monitor open their view when a session exists")
    check(g("action", 0, 0, 0) == 0 and g("action", 7, 2, 2) == 0, "tapping nothing does nothing")
    check([g("action_view", a) for a in range(7)] == [0, 1, 2, 3, 4, 1, 1], "action -> view")
    check("SSH" in cstr(g("hint_addr", 5)) and "Screen" in cstr(g("hint_addr", 6)) and cstr(g("hint_addr", 1)) == "", "hints only where an action needs one")
    check([g("back", v) for v in range(5)] == [-1, 0, -1, -1, 0], "back: arcade and devices -> garage, garage -> leave the app")
    # touch controls
    keys = [g("touch_key", b) for b in range(1, g("touch_count") + 1)]
    check(keys == [65, 68, 87, 83, 32, 74, 16, 75, 80, 13, 27], "touch buttons map to BRAWLPIT's own keys (A D W S Space J Shift K P Enter Esc)")
    check(len(set(keys)) == len(keys) and g("touch_key", 0) == 0 and g("touch_key", 12) == 0, "no two buttons share a key; unknown buttons map to nothing")
