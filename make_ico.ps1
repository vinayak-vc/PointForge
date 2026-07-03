Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile("C:\UnrealProject\PointForge\images\vx.bmp")
$bmp = [System.Drawing.Bitmap]::new($img)

# The user requested 'black like svg', but the SVG has a transparent background (fill="none"). 
# Dark themes make it appear black. We'll make it properly transparent for Windows conventions, 
# which will appear black in dark mode just like the SVG.
$bmp.MakeTransparent([System.Drawing.Color]::FromArgb(255, 0, 255))

$icoFile = [System.IO.FileStream]::new("C:\UnrealProject\PointForge\images\vx.ico", [System.IO.FileMode]::Create)
$icon = [System.Drawing.Icon]::FromHandle($bmp.GetHicon())
$icon.Save($icoFile)
$icoFile.Close()
$bmp.Dispose()
$img.Dispose()
$icon.Dispose()
