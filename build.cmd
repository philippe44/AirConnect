setlocal

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars32.bat"


if /I [%1] == [rebuild] (
	set option="-t:Rebuild"
)

msbuild AirConnect.sln /property:Configuration=Release %option%
msbuild AirConnect.sln /property:Configuration=Static %option%

endlocal