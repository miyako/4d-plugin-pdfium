//%attributes = {}
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"sample.pdf"
$options:=New object:C1471("scale";3;"dpi";96*3)

$col:=PDFium Test ($path;$options)

C_PICTURE:C286($image)
$i:=1

For each ($image;$col)
	$path:=System folder:C487(Desktop:K41:16)+String:C10($i)+"_288.png"
	WRITE PICTURE FILE:C680($path;$image)
	$i:=$i+1
End for each 