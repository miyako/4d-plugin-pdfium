![version](https://img.shields.io/badge/version-20%2B-E23089)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-pdfium)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-pdfium/total)

### Dependencies and Licensing

* the source code of this plugin developed using the [4D Plug-in SDK](https://github.com/4d/4D-Plugin-SDK) is licensed under the MIT license.
* see [pdfium](https://pdfium.googlesource.com/pdfium/+/main/LICENSE) for the licensing of **pdfium** (Apache License 2.0).
* the licensing of the binary product of this plugin is subject to the licensing of all its dependencies.

# 4d-plugin-pdfium

The **pdfium** plugin renders the pages of a PDF file to rasterized images using Google's PDFium library, returning one in-memory `Picture` (PNG-encoded) per page in a `Collection`. It's intended for generating page previews, thumbnails, or print-resolution rasterizations of a PDF directly from 4D code, without shelling out to another application.

| Command | Returns | Purpose |
|---|---|---|
| [`pdf to image`](#pdf-to-image) | Collection | Renders every page of a PDF file to a PNG `Picture`, one collection element per page |

**Platforms:** Windows, macOS. The plugin doesn't call any platform-version-gated system API (no minimum-OS-version requirement could be identified from the source); the one platform-specific code path (see below) affects internal path resolution only, not the command's visible behavior or signature.

---

## Requirements & platform notes

- The plugin exposes a **single command**, `pdf to image`. There is no version of it that takes more or fewer parameters than documented below.
- `file` is **mandatory** and must be a `4D.File` object — passing a `4D.Folder`, a plain text path, or `Null` will not resolve to a path internally and the command will behave as if the file doesn't exist (see Error handling).
- `options` is **optional**; when omitted, rendering defaults to 72 dpi with an opaque white background.
- **Internal-only macOS path handling:** resolving the input `4D.File` object to a native filesystem path takes an extra step on macOS (re-reading the resolved object's `path` property after the initial `platformPath` lookup) that Windows doesn't need. This is transparent to the caller — it doesn't change what paths you can pass or how the command behaves — but is worth knowing if you're debugging path-resolution issues that only reproduce on one OS.
- **`dpi` and page-size limits are silently enforced, not validated with an error.** See the command's Description and Error handling sections — out-of-range or oversized requests are clamped or skipped rather than raising a 4D error.

---

## `pdf to image`

### Syntax

```4d
result:Collection := pdf to image(file:Object)
result:Collection := pdf to image(file:Object; options:Object)
```

### Parameters

| Parameter | Type | Description |
|---|---|---|
| `file` | Object (4D.File) | The PDF file to render. Must be a `4D.File` object (e.g. from `File(path)` or `Folder(...).file(...)`) — mandatory. |
| `options` | Object | Optional. Properties below; any omitted property uses its default. |
| &nbsp;&nbsp;`options.dpi` | Real | Rendering resolution in dots per inch. Default `72`. Internally clamped to the range **10–1200**; a value outside this range is silently adjusted to the nearest bound rather than rejected. |
| &nbsp;&nbsp;`options.background` | Text | Set to `"none"` for a transparent background. Any other value, or omitting the property, renders an opaque white background. |
| Result | Collection | One element per page, in page order: a `Picture` on success, `Null` for a page that failed to render (see Description). See Error handling for the case where the collection can end up **shorter** than the page count. |

### Description

Each page is rendered independently at `options.dpi` (or 72 if not given), converted from PDFium's native BGRA buffer to RGBA, and PNG-encoded with the requested dpi embedded in the PNG's `pHYs` chunk. The returned `Picture` therefore already carries the correct physical-size metadata for applications that respect it (e.g. when placed in a print layout).

Two independent guards affect whether a given page comes back as a real image or as `Null`:

- **dpi is clamped to 10–1200** before any rendering happens. There is no way to request a dpi outside this range — 4D never sees an error, the effective value is just adjusted.
- **Rendered pixel dimensions are capped at 10,000px per side.** A `dpi`/page-size combination whose result would exceed that on either axis causes that page to be skipped — its collection element is `Null` — rather than attempting the (potentially very large) allocation.

If the bitmap for a page can't be allocated for any other reason, or the page itself fails to load, that page's element is likewise `Null`. Other pages in the same document are unaffected and continue to render normally.

If `file` doesn't resolve to a loadable PDF at all (wrong type of object, file doesn't exist, not a valid PDF), the command returns an **empty collection** — there's no element per intended page, since the page count is never known.

### Example

From the plugin's own test method (`test.4dm`):

```4d
//%attributes = {}
$file:=File:C1566("/RESOURCES/4Dv20_LTS_brochure_English.pdf")
$file:=Folder:C1567(fk desktop folder:K87:19).file("Logo.pdf")


$images:=pdf to image($file; New object:C1471("dpi"; 300; "background"; "none"))

$i:=0
For each ($image; $images)
	$i:=$i+1
	//TRANSFORM PICTURE($image;Scale;72/300;72/300)
	WRITE PICTURE FILE:C680(Folder:C1567(fk desktop folder:K87:19).platformPath+"page"+String:C10($i)+".png"; $image)
End for each
```

The commented-out `TRANSFORM PICTURE` line is a useful pattern worth calling out: render once at a high dpi for quality, then scale the resulting `Picture` back down (here, 300 dpi → a nominal 72 dpi) if you need a smaller on-screen copy without re-rendering the page from the PDF.

Rendering at the default resolution, with no options:

```4d
$file:=File("/RESOURCES/sample.pdf")
$images:=pdf to image($file)

// $images.length equals the PDF's page count, unless the file failed to load
// $images[0] is the Picture for page 1, etc.
```

Skipping pages that failed to render:

```4d
$file:=File("/RESOURCES/report.pdf")
$images:=pdf to image($file; New object("dpi"; 150))

$i:=0
For each ($image; $images)
	$i:=$i+1
	If ($image=Null)
		ALERT("Page "+String($i)+" could not be rendered.")
	End if
End for each
```

---

## Error handling & troubleshooting

- **Invalid or unresolvable `file` returns an empty collection, not an error.** There's no 4D error raised if the PDF can't be opened — check `$images.length=0` (or compare it against the page count you expect) rather than wrapping the call in error-handling for a thrown exception.
- **A `Null` element means that specific page failed — the call itself didn't fail.** Always test each collection element (`If ($image=Null)`) before using it; don't assume every element is a `Picture` just because the collection is non-empty.
- **The collection can come back shorter than the actual page count.** If an internal error occurs partway through rendering (for example, an unexpectedly large allocation failing), the plugin stops processing further pages and returns whatever was already rendered — it does not pad the remainder with `Null`. Don't assume `$images.length` always equals the PDF's page count; if you need to detect this case, compare the returned length against the page count from another source if that matters to your workflow.
- **`dpi` outside 10–1200 is silently clamped, not rejected.** If your renders look lower- or higher-resolution than requested, check whether the requested value was outside that range.
- **A dpi/page-size combination exceeding 10,000px per side produces a `Null` for that page**, not a very slow or very large render. Lower the `dpi` for oversized pages if you hit this.
- **`file` must be a `4D.File`, not a `4D.Folder` or a text path.** Passing the wrong object type won't raise an error either — it behaves the same as an unresolvable file (empty collection).

---

## Quick reference

```4d
// Render at 300 dpi, transparent background, save each page as PNG
$file:=File("/RESOURCES/sample.pdf")
$images:=pdf to image($file; New object("dpi"; 300; "background"; "none"))

$i:=0
For each ($image; $images)
	$i:=$i+1
	If ($image#Null)
		WRITE PICTURE FILE(Folder(fk desktop folder).platformPath+"page"+String($i)+".png"; $image)
	End if
End for each
```
