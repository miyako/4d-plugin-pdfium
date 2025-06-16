![version](https://img.shields.io/badge/version-20%2B-E23089)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-pdfium)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-pdfium/total)

# 4d-plugin-pdfium

Convert PDF pages to PNG images.

```4d
var $file : 4D.File
$file:=File("/RESOURCES/4Dv20_LTS_brochure_English.pdf")
var $images : Collection
$images:=pdf to image($file)
$i:=0
For each ($image; $images)
	$i+=1
	WRITE PICTURE FILE(Folder(fk desktop folder).platformPath+["page"; $i; ".png"].join(""); $image)
End for each 
```
