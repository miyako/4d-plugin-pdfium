# 4d-plugin-pdfium
Proof of concept; convert PDF to PNG

#### gn args

```
pdf_bundle_freetype = true 
pdf_use_skia = false 
pdf_use_skia_paths = false 
is_component_build = true 
pdf_is_complete_lib = true
is_component_build=false
pdf_enable_v8 = false 
pdf_enable_xfa = false 
pdf_is_standalone = true
clang_use_chrome_plugins = false
```

using static ``libpng`` for DPI, because Chrominum excludes ``pHYs`` support.
