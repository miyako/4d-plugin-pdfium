# 4d-plugin-pdfium
Proof of concept; convert PDF to PNG

### Platform

| carbon | cocoa | win32 | win64 |
|:------:|:-----:|:---------:|:---------:|
||<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|<img src="https://cloud.githubusercontent.com/assets/1725068/22371562/1b091f0a-e4db-11e6-8458-8653954a7cce.png" width="24" height="24" />|

### Version

<img width="32" height="32" src="https://user-images.githubusercontent.com/1725068/73986501-15964580-4981-11ea-9ac1-73c5cee50aae.png"> <img src="https://user-images.githubusercontent.com/1725068/73987971-db2ea780-4984-11ea-8ada-e25fb9c3cf4e.png" width="32" height="32" />

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
