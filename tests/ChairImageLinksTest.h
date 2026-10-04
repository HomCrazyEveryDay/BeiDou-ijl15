#pragma once

Value ChairLookup(void* manager, const wchar_t* path, bool original = false) {
    Value value;
    VARIANT missing{}; missing.vt = VT_ERROR; missing.scode = DISP_E_PARAMNOTFOUND;
    BSTR name = SysAllocString(path);
    const auto getter = original ? MixedDye::originalGetObject : Method<ChairImageLinks::Getter>(manager, 0x1c);
    auto hr = getter(manager, name, missing, missing, &value.v);
    SysFreeString(name); Check(hr);
    return value;
}

void CheckChairImageLinks(void* manager, Factory factory) {
    auto rawValue = ChairLookup(manager, ChairImageLinks::RootPath, true);
    auto raw = Query(rawValue.v, PropertyIID());
    Require(raw.p != nullptr, "actual chair 3015759 loads");
    MixedDye::factory = FailFactory;
    auto fallback = ChairLookup(manager, ChairImageLinks::RootPath);
    Require(Query(fallback.v, PropertyIID()).p == raw.p && !ChairImageLinks::Cached().image.p,
        "chair allocation failure returns intact native resource without partial cache");
    MixedDye::factory = factory;
    auto resolvedValue = ChairLookup(manager, ChairImageLinks::RootPath);
    auto resolved = Query(resolvedValue.v, PropertyIID());
    Require(resolved.p && resolved.p != raw.p, "chair compatibility returns a private property tree");
    int linked = 0, totalDelay = 0;
    size_t bytes = 0;
    for (const auto* effect : {L"effect", L"effect2"}) for (int frame = 0; frame < 15; ++frame) {
        const auto path = std::wstring(effect) + L"/" + std::to_wstring(frame);
        auto before = ChairImageLinks::Read(raw.p, path), after = ChairImageLinks::Read(resolved.p, path);
        auto bc = Query(before.v, CanvasIID()), ac = Query(after.v, CanvasIID());
        Require(bc.p && ac.p, "both chair layers keep every animation frame");
        auto bp = CanvasProperty(bc.p), ap = CanvasProperty(ac.p);
        Value link; Get(bp.p, L"_inlink", &link.v);
        Object pixels;
        if (link.v.vt == VT_BSTR) {
            Require(Int(bc.p, 0x40) == 1 && Int(bc.p, 0x48) == 1 && Pixel(bc.p, 0, 0, 1, 1) == 0,
                "original defect is a transparent 1x1 native canvas");
            auto source = ChairImageLinks::Read(raw.p, link.v.bstrVal + 9);
            pixels.p = Query(source.v, CanvasIID()).detach(); ++linked;
            bytes += size_t(Int(ac.p, 0x40)) * Int(ac.p, 0x48) * 4;
            Value remaining; Get(ap.p, L"_inlink", &remaining.v);
            Require(Empty(remaining.v), "resolved canvas no longer depends on unsupported inlink");
        } else {
            static_cast<IUnknown*>(bc.p)->AddRef();
            pixels.p = bc.p;
            Require(ac.p == bc.p, "unlinked canvases are reused without copying or changing pixels");
        }
        const int w = Int(pixels.p, 0x40), h = Int(pixels.p, 0x48);
        Require(Int(ac.p, 0x40) == w && Int(ac.p, 0x48) == h, "linked dimensions match actual source artwork");
        int visible = 0;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const auto expected = Pixel(pixels.p, x, y, w, h), actual = Pixel(ac.p, x, y, w, h);
            Require(expected == actual, "every chair pixel and alpha is preserved exactly");
            visible += (actual >> 24) != 0;
        }
        Require(visible > 100, "every animation frame now contains visible artwork");
        Require(Int(ac.p, 0x6c) == Int(bc.p, 0x6c) && Int(ac.p, 0x74) == Int(bc.p, 0x74),
            "referring frame origin preserved");
        for (const auto& key : Names(bp.p)) {
            if (key == L"_inlink") continue;
            Value b, a; Get(bp.p, key, &b.v); Get(ap.p, key, &a.v);
            Require(a.v.vt == b.v.vt, "all frame metadata types preserved");
            if (b.v.vt == VT_I4) Require(a.v.lVal == b.v.lVal, "frame delay and z preserved");
            else if (b.v.vt == VT_BSTR) Require(wcscmp(a.v.bstrVal, b.v.bstrVal) == 0, "frame strings preserved");
            else {
                auto bv = Query(b.v, VectorIID()), av = Query(a.v, VectorIID());
                Require(bv.p && av.p && Int(av.p, 0x20) == Int(bv.p, 0x20) && Int(av.p, 0x28) == Int(bv.p, 0x28),
                    "native origin vector preserved");
            }
        }
        Value delay; Get(ap.p, L"delay", &delay.v);
        if (wcscmp(effect, L"effect") == 0) totalDelay += delay.v.lVal;
        const auto directPath = std::wstring(ChairImageLinks::RootPath) + L"/" + path;
        auto direct = ChairLookup(manager, directPath.c_str());
        Require(Query(direct.v, CanvasIID()).p == ac.p, "root and direct frame lookups share corrected cached canvas");
        auto directDelay = ChairLookup(manager, (directPath + L"/delay").c_str());
        Require(directDelay.v.vt == VT_I4 && directDelay.v.lVal == delay.v.lVal, "direct canvas metadata lookup works");
    }
    Require(linked == 5 && totalDelay == 1800, "exact five broken frames repaired; 1.8 second loop preserved");
    for (const auto* effect : {L"effect", L"effect2"}) {
        auto originalLayer = ChairImageLinks::Read(raw.p, effect), correctedLayer = ChairImageLinks::Read(resolved.p, effect);
        auto op = Query(originalLayer.v, PropertyIID()), cp = Query(correctedLayer.v, PropertyIID());
        Require(Names(op.p) == Names(cp.p), "layer frame count and ordering unchanged");
        Value before, after; Get(op.p, L"z", &before.v); Get(cp.p, L"z", &after.v);
        Require(before.v.lVal == after.v.lVal, "front and back layer order unchanged");
    }
    // Exercise the resource paths used by the native chair loader, repeatedly.
    for (int i = 0; i < 100; ++i) {
        auto layer = ChairLookup(manager, L"Item/Install/0301.img/03015759/effect");
        auto expected = ChairImageLinks::Read(resolved.p, L"effect");
        Require(Query(layer.v, PropertyIID()).p == Query(expected.v, PropertyIID()).p, "repeated chair lookup reuses cache");
    }
    for (auto path : {L"Item/Install/0301.img/03015060/effect", L"Item/Install/0301.img/03010070/effect"}) {
        auto native = ChairLookup(manager, path, true), actual = ChairLookup(manager, path);
        Require(Query(native.v, PropertyIID()).p == Query(actual.v, PropertyIID()).p, "other chairs retain native resource identity");
    }
    for (auto path : {L"Item/Install/0301.img/030157590", L"Item/Install/0301.img/03015759bad", L"Character/Hair/03015759.img"}) {
        BSTR name = SysAllocString(path); std::wstring suffix;
        Require(!ChairImageLinks::Match(name, suffix), "chair whitelist excludes neighboring IDs and unrelated paths"); SysFreeString(name);
    }
    // Malformed linked data must fail without recursion overflow or altering art.
    auto originalFrame = ChairImageLinks::Read(raw.p, L"effect/0");
    auto canvas = Query(originalFrame.v, CanvasIID()); ChairImageLinks::Limits limits;
    auto fixture = ChairImageLinks::CopyPixels(canvas.p, canvas.p, factory, limits);
    auto metadata = CanvasProperty(fixture.p), tree = New(factory, L"Property", PropertyIID());
    PutObject(tree.p, L"x", fixture.p);
    for (const auto* path : {L"03015759/x", L"03015759/missing", L"03015759/../x", L"03015060/effect/0"}) {
        Value link; link.v.vt = VT_BSTR; link.v.bstrVal = SysAllocString(path); Put(metadata.p, L"_inlink", link.v);
        bool rejected = false;
        try { auto result = ChairImageLinks::PixelSource(tree.p, fixture.p); } catch (...) { rejected = true; }
        Require(rejected, "cyclic, missing, escaping or cross-item link is rejected safely");
    }
    auto untouched = ChairLookup(manager, L"Item/Install/0301.img/03015759/effect/8", true);
    auto untouchedCanvas = Query(untouched.v, CanvasIID());
    Require(Int(untouchedCanvas.p, 0x40) == 1, "original shared resource remains unchanged after all requests");
    std::printf("PASS chair 3015759: five transparent frames repaired, 30 frames pixel-exact, 1800ms loop, cache bytes=%zu, failure and cycle guards\n", bytes);
}
