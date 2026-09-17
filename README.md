# Type2DK UWB Ranging

Murata Type2DK EVK（確認基板: Rev.4.1）3台で、COM19からCOM21・COM22までの距離を取得するファームウェアです。現在の動作確認済み構成を `v1.0.0` として保存しています。

| ノード名 / 既定COMポート | UWB短縮アドレス | 役割 | BIN |
|---|---|---|---|
| 19 / COM19 | `0x1111` | 2セッションのResponder・距離出力 | [node19](firmware/v1.0.0/2dk_range_node19_v1.bin) |
| 21 / COM21 | `0x2222` | 19へのInitiator | [node21](firmware/v1.0.0/2dk_range_node21_v1.bin) |
| 22 / COM22 | `0x3333` | 19へのInitiator | [node22](firmware/v1.0.0/2dk_range_node22_v1.bin) |

COM番号はPC側の接続名です。ファームウェアのノード番号・UWBアドレスとは別に変更できます。相手はこの2台を固定設定しており、任意の新規ノードを自動発見する機能ではありません。

## 現在の機能

- COM19–21、COM19–22の直接測距。各200 ms周期、COM19の表示は約100 ms周期。
- 通常動作に終了時間の制限なし。
- 一方の相手が切断しても、もう一方の測距を継続。停滞したセッションを再起動し、相手復帰時に再取得。
- 最後の有効測定から3秒を超えると、該当距離を無効化。
- UWB API異常が続いた場合は基板を自動再起動。RTOSが動作していることを前提とするソフトウェア監視も搭載。

21–22の測距、UWBアプリケーションデータ転送、加速度XYZの読み出しは含みません。

## 書き込み

WindowsにNXP DK6ProductionFlashProgrammerをインストールし、シリアルモニターを閉じて3台を接続します。このリポジトリをダウンロード・展開したフォルダで実行します。

```powershell
# 検査と割り当て確認のみ（書き込みなし）
powershell -ExecutionPolicy Bypass -File .\scripts\flash_all.ps1 -DryRun

# COM19、COM21、COM22へ順番に書き込み
powershell -ExecutionPolicy Bypass -File .\scripts\flash_all.ps1
```

Programmerの既定位置は `C:\NXP\DK6ProductionFlashProgrammer\DK6Programmer.exe` です。異なる場合は `-Programmer` で指定します。COM番号は `-Port19 COM19 -Port21 COM21 -Port22 COM22` で変更できます。

全BINのサイズ・SHA256とポート重複を検査してから書き込みを開始します。途中で失敗するとその場で停止します（先に完了した基板は書き込み済みです）。USB機器の個体識別はしていないため、COMポートと実機の対応は確認してください。完了後に3台を電源再投入します。

書き込みスクリプトは事前検査を自動テストしています。スクリプトからの実機書き込みはこの環境では未検証です。

## COM19の読み取り

**3,000,000 bps / 8N1 / フロー制御なし**で開きます。Programmerの通信速度1,000,000 bpsとは異なります。

```text
RANGE,SEQ=123,MS=12345,D19_21=121,D19_22=184,DV=3,AGE21=50,AGE22=110
```

| 項目 | 意味 |
|---|---|
| `D19_21`, `D19_22` | 距離 [cm]。無効・未取得は `65535` |
| `DV` | 有効ビット。`0`: 両方無効、`1`: 21のみ、`2`: 22のみ、`3`: 両方有効 |
| `AGE21`, `AGE22` | 有効測定からの経過時間 [ms]。無効時 `65535` |
| `SEQ`, `MS` | 出力連番、起動後時間 [ms] |

`BOOT`、`HEALTH`、`RECOVER`などの診断ログも出ます。距離を処理する場合は `RANGE,` 行を読み、必ず `DV` を確認してください。

## GitHubでBINを生成

Actionsの **Build BINs** で3台分をコンパイルし、BIN・検査用manifest・Windows書き込みスクリプトをArtifactsへ保存します。初回のみ、利用許諾を取得したSDKを非公開で取得するための `SDK_DOWNLOAD_URL` Secretが必要です。設定手順とローカルビルド方法は [docs/BUILD.md](docs/BUILD.md) にあります。

SDK未設定の場合、push時のビルドは「未設定」と表示してスキップします。既存BINのコピーを新規ビルドの成功として扱いません。**Validate source and baseline BINs** はSDKなしで動作します。

GitHubのクラウド実行環境からPCのCOMポートへは直接接続できません。生成Artifactをダウンロード・展開して、その中の `flash_all.ps1` をWindowsで実行できます。完全自動書き込みには別途PC側の実行環境が必要です。

## 検証範囲

`v1.0.0` の保存BINは、提供された実機ログで両距離の取得と、21・22それぞれの切断後の復帰を確認しています。長期連続運転や距離精度の保証を意味しません。

自動テストでは、各ノードの360秒相当の継続、20秒の相手不在からの復帰、他方の測距維持、古い値の無効化、時刻カウンタ周回、SDK API異常からの再起動判断を確認します。ホスト上の模擬試験であり、無線実機試験とは別です。

SDK: `UWBIOT_SR040_v04.03.14_MCUx`、コンパイラ: Arm GNU Toolchain `13.2.Rel1`。NXP SDK本体・Murata配布ZIP・ツールチェーンはリポジトリに含めていません。BINに含まれる第三者ソフトウェアについては [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
