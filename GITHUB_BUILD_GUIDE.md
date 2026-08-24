# Fixed GitHub Actions build

The previous workflow failed because it looked for `vswhere.exe` at:

`C:\Program Files\Microsoft Visual Studio\Installer\vswhere.exe`

The corrected workflow uses the official `microsoft/setup-msbuild@v2`
GitHub Action instead, so MSBuild is placed on PATH automatically.

## Run

1. Push/upload this package to GitHub.
2. Open **Actions**.
3. Select **Build MT4 AutoTrading Emergency Manager**.
4. Click **Run workflow**.
5. Wait for the build to finish.
6. Download artifact:
   `MT4_AutoTrading_Emergency_Manager-windows-x64`

The artifact contains:
`MT4_AutoTrading_Emergency_Manager.exe`
