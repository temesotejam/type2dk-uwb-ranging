# ビルド手順

## 入力ファイル

利用許諾を取得した次の配布ファイルを用意します。このリポジトリはSDKを配布しません。

| ファイル | SHA256 |
|---|---|
| `UWBIOT_SR040_v04.03.14_MCUx.zip` | `e2f021fe1e59bf3c84bc9480599000a52ad079b5a24e23dc385cf650e68bb8dc` |
| `Standalone binary for Type2DK EVK(v04.03.14)_updated.zip` | `9c233f6ed78f41afa84678f0511f44fec4581dfe61cf4b26d7037b554f433d71` |

Murata ZIP内のボード対応パッチを適用し、`apply_sdk_changes.py` でFreeRTOSヒープなどの統合設定を適用します。同スクリプトには既存のSPIアクセス用フックも残しています。測距アプリ自体は加速度データを取得しません。

## GitHub Actionsの初回設定

1. Python 3.10以降で、手元の2つのZIPを非公開のビルド入力パッケージにまとめます。入力ZIPのSHA256は自動検査されます。

   ```powershell
   python build/sdk_bundle.py pack --sdk-zip "C:/NXP/UWBIOT_SR040_v04.03.14_MCUx.zip" --murata-zip "C:/NXP/Standalone binary for Type2DK EVK(v04.03.14)_updated.zip" --out "C:/NXP/type2dk-build-inputs.zip"
   ```

2. `type2dk-build-inputs.zip` を利用権限のある非公開ストレージに置き、GitHub ActionsからダウンロードできるHTTPS URLを取得します。例: 非公開オブジェクトストレージの署名付きURL。ZIP自体はこの公開リポジトリ・Release・公開Artifactへアップロードしません。
3. リポジトリの **Settings → Secrets and variables → Actions → New repository secret** で、名前を `SDK_DOWNLOAD_URL`、値をそのURLに設定します。ダウンロードにブラウザでのログイン操作が必要な共有ページURLは使えません。期限付きURLは失効時に更新してください。
4. **Actions → Build BINs → Run workflow** を実行します。以後は `main` へのpushでもビルドします。
5. 成功した実行の **Artifacts → type2dk-bins-<commit SHA>** をダウンロードします。

取得したパッケージ内の2つのZIPを、上記の固定SHA256と照合してから展開・ビルドします。SDKやELF、デバッグ用パスを含むコンパイルログはArtifactに公開しません。公開されるのはBIN・manifest・SHA256・書き込みスクリプト・ライセンス文書です。新しく生成したBINは、実機試験前として記録します。

SDK Secretがない状態では、クラウド上のファームウェアコンパイルは実行できません。手動起動時は理由を表示して失敗し、push時は生成ジョブをスキップします。

## ローカルでビルド（Linux / WSL）

Python 3.10以降、`patch`、`gcc`（ホストテスト用）と、Arm GNU Toolchain 13.2.Rel1のx86_64 Linux / arm-none-eabi版を使用します。MCUXpresso IDEは不要です。

```bash
python3 build/prepare_sdk.py \
  --sdk-zip /path/to/UWBIOT_SR040_v04.03.14_MCUx.zip \
  --murata-zip '/path/to/Standalone binary for Type2DK EVK(v04.03.14)_updated.zip' \
  --out .deps/sdk

bash validation/host/run_tests.sh

python3 build/build_all.py \
  --sdk .deps/sdk \
  --gcc-bin /path/to/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin \
  --out out
```

`prepare_sdk.py` は新しい出力先を必要とします。再ビルドでは準備済みの `.deps/sdk` をそのまま使用できます。BINの出力先は `out/firmware` です。

保存済みの `firmware/v1.0.0` は実機で使用したBINそのものです。SDKが埋め込むビルド元のパスやリンク順序により、別ディレクトリでの再ビルドではサイズ・SHA256が変わることがあります。バイト単位の同一性は保証しません。保存BINの検証には同じフォルダのmanifestを使い、新規ビルドは生成されたmanifestを使います。

```powershell
# ArtifactのZIPを展開したフォルダ内で実行
powershell -ExecutionPolicy Bypass -File .\flash_all.ps1 -DryRun
powershell -ExecutionPolicy Bypass -File .\flash_all.ps1
```

## 実機への完全自動書き込み

GitHubが提供するクラウドrunnerには、手元のUSB/COMポートは見えません。書き込みは2DKを接続したWindows PCで実行する必要があります。

この公開リポジトリには、PC上で任意のActionsジョブを実行するself-hosted runnerは登録していません。完全自動化する場合は、書き込み専用の非公開リポジトリと専用Windows runnerを別途構成し、検証済みの成果物を取得して本スクリプトを実行する運用にできます。[GitHubのself-hosted runner説明](https://docs.github.com/en/actions/hosting-your-own-runners)、[公開リポジトリでの注意事項](https://docs.github.com/en/actions/reference/security/secure-use)。
