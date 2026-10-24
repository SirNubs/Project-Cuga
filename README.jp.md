> **Project Cuga** is a fork of [francl35/Poke](https://github.com/francl35/Poke). This Japanese page is still Poke's own README and has not been rewritten for the fork; see [README.md](README.md) for the current description. / このページは上流 Poke の README のままです。最新の説明は README.md を参照してください。

# Poke

**[English](README.md) | [日本語](README.jp.md)**


**Poke** は、PSP 用ゲーム **『モンスターハンターポータブル 2nd G』**（MHP2G、海外版タイトル：*Monster Hunter Freedom Unite*）を対象とした、実験的なネイティブ再コンパイルプロジェクトです。

Poke は **[TeamGDB/Yakumo](https://github.com/TeamGDB/Yakumo) を基にした派生プロジェクト**です。PSP ランタイム、再コンパイルの仕組み、ホスト側実装の大部分は、Yakumo の原作者および貢献者による成果を基盤としています。**Poke は Yakumo のオリジナルプロジェクトではなく、Yakumo チームが保守するものでも、公式な後継プロジェクトでもありません。** オリジナルプロジェクトのリリース、開発、サポートについては、上流の Yakumo を参照してください。元の成果に対する権利と功績は、それぞれの作者および貢献者に帰属します。

このフォークでは、Yakumo の技術を MHP2G に適用することに重点を置いています。リポジトリには上流から引き継いだ MHP3rd プロファイルも残っていますが、これは Poke の MHP2G ビルドが Yakumo 公式のリリースであることを意味しません。

## 現在の開発状況

**開発中 — 一般向けの正式リリースではありません。**

- MHP2G の実行ファイルを解析し、主要なコードをネイティブ C++ として生成しています。
- Linux の Vulkan ビルドで起動に成功し、村でのプレイを確認しています。
- クエスト、戦闘、エリア移動、オーバーレイの互換性については、引き続き検証中です。
- MHP2G の CMake ターゲット名と実行ファイル名は、どちらも `Poke` です。
- 一部のゲストコードはインタープリターで実行される場合があり、すべての処理が事前コンパイル（AOT）で実行されるわけではありません。

上記は開発中のテスト結果であり、ゲーム全体の完全な互換性や、各プラットフォームへの対応を保証するものではありません。

## ゲームファイルと法的注意事項

**このリポジトリには、著作権で保護されたゲームコンテンツは含まれていません。** 『モンスターハンターポータブル 2nd G』の適法に入手したご自身のゲームから、必要なファイルを用意してください。ゲームディスクのイメージ、復号済み実行ファイル、抽出したアセット、ゲームから生成した C++ コード、コンパイル済みのゲーム用オーバーレイは配布しておらず、リポジトリにコミットしてはいけません。

Poke は非公式のファン制作による互換性プロジェクトであり、株式会社カプコン、ソニー、またはその関連会社との提携、承認、推奨、後援関係はありません。『モンスターハンター』、『モンスターハンターポータブル 2nd G』、*Monster Hunter Freedom Unite*、PSP、および関連する名称やアセットの権利は、それぞれの権利者に帰属します。ご自身のゲームファイルの準備と使用にあたっては、適用される法令を遵守してください。

## Linux での MHP2G ビルド（開発用）

必要なものには、C++20 対応の開発環境、CMake、Python 3、SDL3、Vulkan の開発用ファイル、シェーダーのコンパイルツールなどがあります。MHP2G のレンダラービルドでは、メディア処理に FFmpeg も使用します。その他の依存関係については、CMake の設定および既存のビルドドキュメントを参照してください。

開発ツールとレンダラーを設定・ビルドします。

```bash
/usr/bin/cmake -S . -B out/mhp2g-renderer -G "Unix Makefiles" \
  -DPSPRECOMP_PROFILE=mhp2g -DMHP2G_RENDERER=ON
/usr/bin/cmake --build out/mhp2g-renderer --target Poke -j"$(nproc)"
```

**ご自身で用意した復号済みの MHP2G `EBOOT.ELF`** から、メインコードをローカルで事前コンパイル（AOT）用に生成します。

```bash
profiles/mhp2g/scripts/generate.sh \
  "$PWD/out/mhp2g-renderer" \
  "/path/to/your/EBOOT.ELF"
```

コード生成後、実行ファイルを再ビルドします。

```bash
/usr/bin/cmake --build out/mhp2g-renderer --target Poke -j"$(nproc)"
```

ローカルのゲームファイルが入ったディレクトリを指定して実行します。

```bash
./out/mhp2g-renderer/bin/Poke "/path/to/your/game-directory"
```

**オーバーレイの生成については、Poke 単体で動作する手順として、まだ検証・文書化が完了していません。** プロファイルにはオーバーレイのビルドスクリプトが含まれていますが、ゲームイメージのパスや出力先の前提条件を現在調整しています。対応が完了するまでは、一部のコードがインタープリター経由で実行される場合があります。生成したゲームコードやオーバーレイライブラリを、Git で追跡されるソースディレクトリにコピーしたり、コミットしたりしないでください。

## プロジェクト構成

- `profiles/mhp2g/` — MHP2G 用のホストコード、ツール、シェーダー、スクリプト、テスト。
- `profiles/mhp3rd/` — 上流プロジェクトから引き継いだ MHP3rd プロファイル。
- `include/psprecomp/`、`src/`、`tools/` — 共通の PSP ランタイムと再コンパイル基盤。
- `docs/` — プロジェクトのドキュメント。一部には上流の MHP3rd に関する記述が残っているため、検証済みの MHP2G 向け手順として扱わないでください。

## クレジットとライセンス

Poke は **[TeamGDB/Yakumo](https://github.com/TeamGDB/Yakumo)** を基にしています。Yakumo は独立したプロジェクトであり、このフォークの保守者は Yakumo の開発者や貢献者を代表する立場にはありません。オリジナルのランタイム、ツール、実装に取り組んだ Yakumo チームおよび上流のすべての貢献者に感謝します。

上流プロジェクトは **MIT ライセンス**で配布されています。`LICENSE` および該当するソースファイルに記載された著作権表示と許諾表示を保持してください。サードパーティ製コンポーネントには個別のライセンスや注意事項が適用される場合があります。付属するライセンスファイルを確認してください。**ソフトウェアのライセンスは、カプコンやソニーのゲームコンテンツを配布する権利を付与するものではありません。**

上流 Yakumo のドキュメントやリリースについては [TeamGDB/Yakumo](https://github.com/TeamGDB/Yakumo) を、このフォークについては [francl35/Poke](https://github.com/francl35/Poke) を参照してください。
