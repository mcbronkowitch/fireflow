# Glow faceplate physical master

`hardware/glow-faceplate/` is the physical design master for the removable
Glow faceplate. A reviewed render of that physical design may later be used for
the VCV faceplate layer; the VCV image is never a fabrication source.

## Physical ownership model

- **Fixed Touch 2 rear PCB:** Synthux hardware. It includes the exposed Daisy
  Seed, Touch identity, and P10/P11. FireFlow may use verified geometry for
  clean-room work, but does not claim ownership of this hardware, its wordmark,
  or its artwork.
- **Removable Glow faceplate:** FireFlow owns the new faceplate artwork and its
  KiCad fabrication source. The outline, diagonal opening, holes, and slots
  remain constrained by the official Synthux mechanical reference.
- **Lower touch PCB:** Synthux hardware. Its P00-P09 electrode silhouettes and
  decorative artwork are reference material; any public reproduction requires
  written Synthux approval or clean-room replacement.
- **Runtime state and switch layer:** FireFlow owns the VCV-only state feedback
  and transparent switch-frame treatment. It changes no fixed Touch 2 hardware
  and does not grant rights to Synthux identity or artwork.

## Production gates

Every gate stays `no` until the linked evidence exists in this repository or in
an approved external record. A gate may not be inferred from public source-code
availability.

| Gate | Status | Required evidence before opening |
| --- | --- | --- |
| `RIGHTS_APPROVED` | `no` | Written Synthux approval for the specific photography, wordmark, electrode-artwork, and attribution use proposed for public distribution. |
| `LABELS_FROZEN` | `no` | Approved, final macro, fader, and switch labels linked to the hardware mapping. |
| `KICAD_AVAILABLE` | `no` | Captured `kicad-cli` path and version from an existing installation or a user-approved installation. |
| `SYNTHUX_PROFILE_APPROVED` | `no` | Written Synthux manufacturing profile covering thickness, mask, finish, copper treatment, minimum features, and panelisation. |

## Tooling and export policy

This project requires **KiCad 8 or later**. The current preflight did not find
`kicad-cli`; Tasks 2-7 may continue, while Task 8 requires a user-approved
installation or an absolute path to an existing CLI.

Gerbers, drill files, and production fabrication packages cannot be generated
while any production gate is `no`. Mechanical study files and clean-room
development outputs remain non-production until all four gates have evidence.

## Restricted reference material

Official DXF, PDF, and frontal images are local-only under
`.reference/glow-touch2/`. They are intentionally ignored. Their byte hashes
and permitted-use record are committed in [sources/provenance.md](sources/provenance.md)
and [sources/checksums.sha256](sources/checksums.sha256).
