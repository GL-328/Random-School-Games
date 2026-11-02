# Optional content packs

Each directory is a downloadable, **data-only** content pack for one game
(directory name = game id). The release workflow zips each pack, uploads it to
the GitHub Release and lists it (URL, size, SHA-256) in `manifest.json`.

* `pack.json` – pack metadata (`version`, `name`, `notes`). Bump `version` to ship an update.
* `theme.json` – colours read by the game (`#RRGGBB`).

Allowed file types: json, txt, png, jpg, webp, wav, ogg, ttf, otf, bin, csv, svg.
Executable content is rejected by the app's extractor.
