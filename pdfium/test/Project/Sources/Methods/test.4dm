//%attributes = {}
var $file : 4D:C1709.File
$file:=File:C1566("/RESOURCES/4Dv20_LTS_brochure_English.pdf")

var $images : Collection
$images:=pdf to image($file)

$i:=0
For each ($image; $images)
	$i+=1
	WRITE PICTURE FILE:C680(Folder:C1567(fk desktop folder:K87:19).platformPath+["page"; $i; ".png"].join(""); $image)
End for each 