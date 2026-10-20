# SDK patches

`rexglue-development-saintsrow.patch` is a `git format-patch` series against ReXGlue
`development` at `bd833a2`. Apply it with `git am` (the README's Step by step does). The same
commits live on the `saintsrow/patches` branch of a local SDK checkout.

| Commit | Why |
| --- | --- |
| skip zero-length indirect buffers | `RingBuffer` with capacity 0 divides by zero in `set_write_offset`; hardware treats an empty IB as a no-op. |
| don't reset the image heap when reading the XEX | `Reset()` wipes the page table for the whole `0x8xxxxxxx` heap, including the function dispatcher allocation at `image_end`. A static recomp loads one module. |
| re-attach native objects when the guest rewrites a dispatch header | Saints Row re-initialises its IO event and semaphore headers inline between waits; the SDK minted a new native object per call (201 for one event), so waiters and signallers never met. |
| log unregistered indirect call targets instead of aborting | One run lists every missing manifest entry instead of one per build. Revert once the manifest is complete. |

Regenerate after changing the branch:

```
git -C tools/rexglue-sdk format-patch --stdout origin/development..saintsrow/patches > docs/sdk-patches/rexglue-development-saintsrow.patch
```
