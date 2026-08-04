//%attributes = {}
$file:=File:C1566("/RESOURCES/4Dv20_LTS_brochure_English.pdf")

$images:=pdf to image($file; New object:C1471("dpi"; 300; "background"; "none"))

$i:=0
For each ($image; $images)
	$i:=$i+1
	//TRANSFORM PICTURE($image;Scale;72/300;72/300)
	WRITE PICTURE FILE:C680(Folder:C1567(fk desktop folder:K87:19).platformPath+"page"+String:C10($i)+".png"; $image)
End for each 