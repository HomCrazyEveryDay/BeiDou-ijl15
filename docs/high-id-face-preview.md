# High ID face preview notes

## Problem

After adding many high ID base faces, NPC face preview pages could crash or fail to update. The most visible case was the face preview NPC: page 9 and later contained `50000+` face IDs. Old faces could preview, and `!face 53086` worked, but clicking next page in the NPC preview did not change the visible face.

The final working behavior:

- High ID face preview no longer crashes.
- Clicking next page changes the preview face.
- Confirming the selection still changes the real character face.

## Root Cause

NPC style preview calls the avatar layer builder at `0x00407757`.

For high ID faces, passing the high face ID as `a3` makes the client use an item-preview style path. That path derives additional face IDs, such as offsets around `+2000` or `+10000`, and can crash with newer face structures.

Changing `a3` to `0` avoids the crash, but by itself it does not refresh the displayed face. The key detail is the avatar buffer layout:

- Successful real character rebuilds store the active face at `a4 - 2`.
- NPC preview input stores the requested preview face at `a4 - 3`.
- `a4 - 2` still contains the current visible face.

An intermediate attempt temporarily wrote the target face to `a4 - 2` and restored it immediately after `AvatarLayerBuild` returned. That still did not update the preview, which suggests the face layer is read again after the direct call or by a delayed rebuild. The final fix leaves the preview buffer's `a4 - 2` set to the requested face.

## Final Fix

The fix is in `ezorsia/AutoTypes.h` and is installed from `ezorsia/dllmain.cpp` through `HookAvatarLayerBuild(true)`.

For high ID face previews:

1. Detect face IDs, including old `20000-29999`, new `50000-59999`, `80000-89999`, and known `4xxxx` face IDs.
2. For old low face IDs, pass through normally.
3. For high face IDs, write the requested face ID into `a4 - 2`.
4. Call the original builder with `a3 = 0` and the original avatar pointer.
5. Keep the `a4 - 2` write in the preview buffer instead of restoring it immediately.

This avoids the crash-prone high ID `a3` path while making the client's later face-layer reads see the requested preview face.

Hair preview still uses the prior proven pattern: copy avatar data, replace hair slot `0`, then call with `a3 = 0`.

## Related Code

- `ezorsia/AutoTypes.h`
  - `IsKnownFacePreviewId`
  - `IsKnownHairPreviewId`
  - `AvatarLayerBuild_Hook`
- `ezorsia/Client.h`
  - `g_facePreviewFaceId`
  - `g_facePreviewFaceId2`
  - `g_facePreviewFaceId3`
- `ezorsia/codecaves.h`
  - `faceHairCave` classifies high and derived face IDs as face resources while a face preview is building.
- `ezorsia/dllmain.cpp`
  - `HookAvatarLayerBuild(true)`

## Investigation Timeline

1. Server packet logs showed `NPC_TALK` style lists were valid. The server was sending IDs such as `53086`, `53186`, `53286`, etc.
2. Client WZ checks showed new face files existed and could be loaded. Normal `!face 53086` changed the character correctly.
3. Direct high ID preview via `a3 = 53086` crashed inside the avatar layer build path.
4. Using `a3 = 0` stopped the crash but did not update the visible face.
5. Path logs showed the client requested the correct files, for example `Character/Face/00053086.img`.
6. Memory scan logs compared:
   - Real character rebuild: new face at `a4 - 2`.
   - NPC preview next page: requested face at `a4 - 3`, current face at `a4 - 2`.
7. Temporarily writing `a4 - 2` and restoring immediately still failed.
8. Keeping `a4 - 2` set to the requested face fixed the preview.

## Tools Used

- PowerShell for searching, building, copying DLLs, and checking client process locks.
- `rg` for fast source searches.
- Visual Studio MSBuild:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' ezorsia.sln /p:Configuration=Release /p:Platform=x86 /m
```

- Visual Studio `dumpbin` for PE header checks and attempted local disassembly.
- Client runtime logs:
  - Server packet logs for `NPC_TALK` and `NPC_TALK_MORE`.
  - Temporary client-side `avatar_preview_debug.log` during investigation.
- `@tybys/wz` for loose `.img` WZ structure checks where needed.

## Cleanup Notes

The final code intentionally does not keep:

- `avatar_preview_debug.log`
- face path logging
- broad avatar slot scan logging
- exception detail logging
- `GetFaceItemPath` hook
- low ID filler workaround in the NPC list

Keep the final fix small. The important behavior is the high face preview branch in `AvatarLayerBuild_Hook`.
