![version](https://img.shields.io/badge/version-20%2B-E23089)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-pdfium)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-pdfium/total)

### Dependencies and Licensing

* the source code of this plugin developed using the [4D Plug-in SDK](https://github.com/4d/4D-Plugin-SDK) is licensed under the MIT license.
* see [pdfium](https://pdfium.googlesource.com/pdfium/+/main/LICENSE) for the licensing of **pdfium** (Apache License 2.0).
* the licensing of the binary product of this plugin is subject to the licensing of all its dependencies.

# 4d-plugin-pdfium

The **pdfium** plugin renders the pages of a PDF file to bitmap images,
returning one picture per page. It is built on Google's PDFium library and is
intended for tasks like generating page thumbnails, previews, or print-ready
rasterizations of PDF documents directly from 4D code — without needing an
external converter or scripting another application.

The plugin initializes the PDFium library once, when it's loaded into the
4D application (and into each 4D Server process, if used in a two-tier or
remote deployment), and releases it on unload.

---

## `pdf to image`

Renders every page of a PDF file to an in-memory image and returns them as a
collection.

### Syntax

```4d
result:Collection := pdf to image(file:Object)
result:Collection := pdf to image(file:Object; options:Object)
```

### Parameters

| Parameter | Type | Description |
|---|---|---|
| `file` | Object (4D.File) | The PDF file to render. Pass a `4D.File` object pointing at the `.pdf` on disk (e.g. from `File(path)` or a `Folder`/`File` navigation). |
| `options` | Object | *(Optional)* Rendering options — see below. |
| `result` | Collection | One element per page of the PDF, in page order (see **Return value**). |

### `options` properties

| Property | Type | Default | Description |
|---|---|---|---|
| `dpi` | Real | `72` | Rendering resolution in dots per inch. Higher values produce larger, sharper images at the cost of memory and render time. **The plugin clamps this internally to the range 10–1200 dpi** — values outside this range are silently adjusted to the nearest bound rather than rejected. |
| `background` | Text | *(opaque white)* | Set to `"none"` to render with a **transparent** background instead of opaque white. Any other value (or omitting the property) keeps the default opaque white fill. |

### Return value

A **collection** with exactly one element per page in the source PDF, in
page order:

- On success, the element is a **Picture** (PNG-encoded) of that page,
  rasterized at the requested `dpi`.
- On failure for a given page — the page couldn't be loaded, or its
  rendered dimensions were invalid or too large (see **Limits** below) — the
  corresponding element is `Null` rather than a picture. This lets you
  detect and skip problem pages without the whole call failing.
- If the input file itself can't be opened/parsed as a PDF, an **empty
  collection** is returned.

### Limits

- Maximum rendered page dimension: **10,000 pixels** per side. A page/DPI
  combination that would exceed this on either axis is skipped (returns
  `Null` for that page) rather than attempting a very large allocation.
- `dpi` is clamped to **10–1200**.

These limits exist to keep a single call bounded in memory and time
regardless of what dpi or page sizes are requested; they are not
independently configurable.

### Performance notes

- Rendering is done page-by-page and is CPU/memory-bound by the requested
  `dpi` and each page's physical size — a full-bleed poster-sized page at
  1200 dpi will take meaningfully longer and use meaningfully more memory
  than a normal document at 72 dpi.
- The plugin yields control back to 4D periodically during pixel conversion
  on large pages so the interface doesn't freeze during long conversions,
  but very large documents/DPI combinations will still take proportionally
  longer to return.

---

## Sample code

### Basic usage — render at default (screen) resolution

```4d
$file:=File("/RESOURCES/sample.pdf")
$images:=pdf to image($file)

// $images.length equals the PDF's page count
// $images[0] is the Picture for page 1, etc.
```

### Render at print resolution (300 dpi), transparent background

This mirrors the plugin's own test sample (`test.4dm`):

```4d
$file:=File("/RESOURCES/4Dv20_LTS_brochure_English.pdf")
$file:=Folder(fk desktop folder).file("Logo.pdf")

$images:=pdf to image($file; New object("dpi"; 300; "background"; "none"))

$i:=0
For each ($image; $images)
	$i:=$i+1
	//TRANSFORM PICTURE($image; Scale; 72/300; 72/300)  // optional: scale back down after high-dpi render
	WRITE PICTURE FILE(Folder(fk desktop folder).platformPath+"page"+String($i)+".png"; $image)
End for each
```

The commented-out `TRANSFORM PICTURE` line above is a useful pattern: render
at a high dpi for quality, then scale the resulting picture back down (here,
from 300 dpi to a nominal 72 dpi) if you need a smaller on-screen image
without re-rendering the page.

### Saving each page as a separate PNG file, skipping failed pages

```4d
var $file : 4D.File
var $images : Collection
var $i : Integer

$file:=File("/RESOURCES/report.pdf")
$images:=pdf to image($file; New object("dpi"; 150))

$i:=0
For each ($image; $images)
	$i:=$i+1
	If ($image#Null)
		WRITE PICTURE FILE(Folder(fk desktop folder).platformPath+"page_"+String($i)+".png"; $image)
	Else
		ALERT("Page "+String($i)+" could not be rendered.")
	End if
End for each
```

### Displaying a page in a form picture variable

```4d
$file:=File("/RESOURCES/contract.pdf")
$images:=pdf to image($file)

If ($images.length>0) & ($images[0]#Null)
	vPreviewPicture:=$images[0]  // bind to a picture variable on your form
End if
```

### Defensive pattern — handling per-page failures

Because individual pages can come back as `Null` (rather than the whole
call failing), always check each element before use:

```4d
var $images : Collection
var $i : Integer
var $failedPages : Collection

$images:=pdf to image(File("/RESOURCES/sample.pdf"))
$failedPages:=New collection

$i:=0
For each ($image; $images)
	$i:=$i+1
	If ($image=Null)
		$failedPages.push($i)  // 1-based page number
	End if
End for each

If ($failedPages.length>0)
	ALERT("Some pages could not be rendered: "+$failedPages.join(", "))
End if
```

---

## Error handling summary

| Situation | Behavior |
|---|---|
| File doesn't exist / isn't a valid PDF | Returns an **empty collection** |
| A specific page fails to load or render | That page's collection element is **`Null`**; other pages are unaffected |
| `dpi` outside 10–1200 | Silently clamped to nearest bound, no error |
| Requested pixel dimensions exceed 10,000px per side | That page's element is `Null` |
| `options` omitted entirely | Renders at 72 dpi with opaque white background |

---

## Version / implementation notes

- Output format is always **PNG**, embedding the requested DPI in the PNG's
  `pHYs` chunk (useful if the image is later opened in an application that
  respects that metadata for print sizing).
- Internally uses Google's PDFium library for parsing/rendering; color
  conversion (PDFium's native BGRA → RGBA) happens in-plugin before PNG
  encoding.
