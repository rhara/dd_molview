[English version](README.md)

# dd_cview

[`dd_molview`](../dd_molview) のデスクトップGUIシェルをネイティブC++/Qt6で
再実装したもの。蛋白質テーブル/リガンドテーブル/3Dビュー/シーケンスビューの
同じ4パネル構成のワークベンチを、ウィンドウ・ドック・テーブル・イベント
ハンドラすべてPySide6ではなくC++/Qtで書き直している。その下にある計算ロジック
（PDB/SDF解析、コンタクト/相互作用検出、スコアリング、RMSD、鎖ごとの配列抽出、
3Dシーン用HTML生成）は**再実装していない**——`dd_cview` は
[pybind11](https://github.com/pybind/pybind11) 経由でPythonインタプリタを
埋め込み、`dd_viewer`/`dd_molview` の既存の、無改変のPythonパッケージを
そのまま呼び出す。境界は1つの狭いJSON入出力モジュール
[`python/dd_cview_backend.py`](python/dd_cview_backend.py) だけに絞っている。

`dd_molview-desktop`（PySide6版）は機能としては完成しているが、インタラ
クティブな操作では体感できるほど動作が遅い——テーブル選択、設定トグル、
残基クリックのすべてが、本当に必要なRDKit/biopython呼び出しだけでなく、
すべてのQtイベントについてPython自身のQtバインディングとシングルスレッド
インタプリタを経由してしまう。`dd_cview` はそれらの呼び出し（実際の処理
時間の大半を占める、コンタクト検出・相互作用の幾何計算・3Dシーン用HTML
生成）はそのまま残し、それ以外——ウィジェット構築、ドックレイアウト、
テーブルモデル、イベント配線、シーケンスパネル、設定パネル、カメラ状態の
管理——をすべてネイティブQt側に移すことで、UIスレッドが不要なPython/Qt
バインディングの往復を待たされないようにしている。

## アーキテクチャ

```
┌─────────────────────────────── C++ / Qt6 ────────────────────────────────┐
│ MainWindow（ドック・メニュー・テーブル・配線）                            │
│  ├─ TableModel        (QAbstractTableModel、TableDataをラップ)           │
│  ├─ DisplaySettingsPanel                                                  │
│  ├─ SequencePanel      (QTextBrowser + クリック可能な残基/鎖リンク)       │
│  ├─ Viewer3D           (QWebEngineViewラッパー、カメラJS往復)            │
│  └─ PythonBridge  ───────────────────────┐                               │
└───────────────────────────────────────────┼──────────────────────────────┘
                                             │ pybind11::embed（JSON文字列のみ）
┌────────────────────────────────────────────┼──────────────────────────────┐
│ python/dd_cview_backend.py :: Session       ▼                             │
│  load_all / *_table_json / sequence_html / build_view_html / ...          │
└──────────────────────┬──────────────────────────────────────────────────┘
                        │ 素の関数呼び出し、無改変
                        ▼
        dd_viewer（解析・相互作用・スコアリング・シーンHTML）
        dd_molview（複数受容体/リガンドのコレクション・配列・ダッシュボード）
```

`PythonBridge`（`src/PythonBridge.h`/`.cpp`）が唯一 `<pybind11/embed.h>`
（つまり `Python.h`）をincludeするファイル——他のすべてのC++ファイルは
Qt/std標準型（`QString`、`QJsonArray`、`TableData`、`DisplaySettings` など）
しか見ず、`py::object` は一切見えない。これは実際のビルド上の障害も回避
している: `Python.h` と Qt の `qobjectdefs.h` はどちらも裸の `slots`
シンボルを定義するため、両方をincludeする翻訳単位はビルドが壊れる——
pybind11のヘッダを非`QObject`の`.cpp`ファイル1つに閉じ込めることで、
あちこちで `QT_NO_KEYWORDS` と戦う代わりにこの問題自体を回避している。

`Session`（Python側の対応物）は `dd_molview.desktop.main_window.MainWindow`
自身の状態とオーケストレーション（`receptor_entries`、`ligand_entries`、
`active_receptor_idx`、`active_ligand_idx`、`reference_mol`、そして
`_refresh_view` 相当の再計算ステップ）を1対1で再現している。C++側の
`MainWindow` が持つのは*表示*状態だけ（どのドックが表示されているか、
設定パネルのチェックボックス、シーケンスパネルの選択残基、3Dビューの
現在のカメラ位置）。`PythonBridge` の各メソッドは文字列（JSON）または
プリミティブ型しかやり取りしないため、pybind11境界自体が単純なまま保たれる
——カスタム型キャスタもなく、`PythonBridge::Impl` の外に長生きする
`py::object` ハンドルも一切存在しない。

## インストール

Qt6（`Core`、`Widgets`、`WebEngineWidgets`、`WebChannel`）、CMake 3.21以上、
C++20対応コンパイラ、そして `dd_molview` 自体が動いているのと同じPython環境
（`dd_viewer` + `dd_molview` がインストール済み・editableかどうかは問わない、
加えてそれらの依存——RDKit、biopython、pandas、numpy、そして `pybind11`。
これは別途システムパッケージとしてではなく、その環境からimportできれば良い）
が必要。

```bash
# Qt6 + ビルドツール (macOS/Homebrew)
brew install cmake qt ninja

# dd_molview/dd_viewer が既に動いているのと同じconda env (フルセットアップは
# ../dd_molview/README.jp.md を参照)
conda activate dd
pip install pybind11   # 未インストールの場合

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

デフォルトでは、CMake構成時に `$CONDA_PREFIX/bin/python3` が指す
Pythonを埋め込む（condaが有効でなければ `~/miniforge3/envs/dd` に
フォールバック）——`dd_viewer`/`dd_molview` がインストールされた別の環境
（別名のconda envやただのvenv）を使う場合は
`-DDD_CVIEW_PYTHON=/path/to/python3` で上書きする。

```bash
cmake -S . -B build -G Ninja -DDD_CVIEW_PYTHON=/path/to/venv/bin/python3
```

**注意**: `dd_molview-desktop` と同様、3DビューはQtの`WebEngineWidgets`
モジュール（`QWebEngineView`）を使うため、Homebrewのフル `qt` フォーミュラ
（Chromiumベースの`qtwebengine`を含む約38パッケージの依存チェーンを引く）
が必要——`qtbase` のみの軽量インストールでは足りない。

## 使い方

```bash
./build/dd_cview \
  --receptor data/6W63_receptor.pdb --poses data/6W63_redock.sdf \
  --receptor data/7L10.pdb --receptor data/7L11.pdb \
  --reference data/6W63_ligand_ref.sdf   # 任意: RMSD計算用
```

`--receptor` と `--poses` はそれぞれ**繰り返し指定可能**（ファイルごとに
フラグを1回ずつ渡す）。`--reference` と `--manifest` はそれぞれ単一指定。
ファイルは起動後にFileメニュー/ツールバー（「Open Receptor(s)...」「Add
Poses...」「Open Reference...」「Open Manifest...」）からも読み込める、
`dd_molview-desktop` と全く同じ挙動。

**posesファイルを一つも指定しなかった場合**、読み込んだ全受容体が埋め込み
リガンドをスキャンされ、結果が1つのリガンドリストにマージされる——
`dd_molview` と同じall-or-nothingな自動抽出ルール（詳細は`dd_molview`の
READMEを参照）。このロジックは `dd_molview.load_all` 内部で無改変のまま
実行され、`dd_cview` はそれを呼び出すだけ。

### アンサンブルmanifest.json

```bash
./build/dd_cview --manifest data/sample_manifest.json
```

`dd_molview` が読むのと同じdd_docking形式の `manifest.json`
（`{member_id, receptor_pdb, ...}` のプレーンなJSONリスト、duck-typing、
`dd_docking` はimportしない）——`--receptor` のパスと加算的に読み込まれる。

## 機能

各パネルは `dd_molview-desktop` と全く同じ挙動をする（同じ
`dd_viewer`/`dd_molview` の呼び出しを、PySide6ではなくネイティブの
`QTableView`/`QWebEngineView`/`QTextBrowser` ウィジェット経由で行っている
だけ）:

- **蛋白質選択パネル**: 読み込んだ受容体ごとに1行（ラベル、鎖数/残基数、
  ソースパス）。行を選ぶとその受容体が3Dビューとシーケンスパネルのアクティブ
  対象になる。
- **リガンド選択パネル**: 読み込んだリガンドごとに1行、スコア/RMSD/相互
  作用数の各列は*アクティブな*受容体に対してのみ計算される。
- **3D構造ビュー**: cartoon/stick/surfaceの切り替え、二次構造による色分け、
  リガンド近傍のみ表示オプション、相互作用オーバーレイ（水素結合、疎水性
  接触、塩橋、静電相互作用、パイスタッキング、ハロゲン結合）、残基ラベルの
  フローティング表示、更新をまたいだカメラ位置の保持——`Viewer3D` が
  `page()->runJavaScript()` の往復で直前に取得したカメラ位置を新しい
  シーンに再適用する、`dd_molview-desktop` と同じ手法。
- **シーケンスパネル**: 1行50残基固定のresnumグリッドと行頭ルーラー、3D
  ビューと同じ黄色/マゼンタの2階層ハイライト、クリック可能な残基
  （Ctrl/Cmd-クリックで複数選択）とクリック可能な「Chain X」見出し
  （その鎖にカメラを再フィット）。
- **コンタクト残基テーブル**（デフォルトで折りたたみ）: 行を選択すると
  同じ残基が3Dビューとシーケンスパネルの両方でハイライトされる。
- **すべてのパネルが独立したドック**——それぞれ独立して移動・フロート化・
  クローズ可能で、セッションをまたいで `QSettings`
  （`saveGeometry`/`saveState`）で状態が復元される。

**カメラの挙動**と**複数残基選択**は `dd_molview-desktop` と全く同じ規約に
従う（詳細は[そちらのREADME](../dd_molview/README.jp.md)を参照）——
アクティブな蛋白質/リガンド行の切り替えや表示設定の変更ではカメラは動かず、
自分でドラッグ/ズームするか、明示的に「Center on Ligand」を押した時だけ動く。

## モジュール構成 (`src/`)

| ファイル | 内容 |
|---|---|
| `PythonBridge.h`/`.cpp` | `<pybind11/embed.h>` に触れる唯一のファイル。埋め込みインタプリタ（`py::scoped_interpreter`）と唯一の `Session` オブジェクトを保持する。各メソッドは `QJsonDocument` とQt/std標準型との間で変換する。 |
| `MainWindow.h`/`.cpp` | ドック構築、メニュー/ツールバー、レイアウト永続化、全Qtシグナルハンドラ、そして中心となる再計算・再描画メソッド `refreshView()`——`dd_molview.desktop.main_window.MainWindow` のC++版。 |
| `TableModel.h`/`.cpp` | `TableData`（列/表示行/生データ行）をラップする、3つのテーブル全てで再利用する読み取り専用の `QAbstractTableModel`。 |
| `DisplaySettingsPanel.h`/`.cpp` | 設定ドックのコントロール群——`dd_molview.desktop.controls.DisplaySettingsPanel` と1対1対応。 |
| `SequencePanel.h`/`.cpp` | シーケンスパネルのフォント/リンク処理設定を持つ `QTextBrowser` サブクラス。 |
| `Viewer3D.h`/`.cpp` | 3Dビューの `QWebEngineView` をラップ: 新しく構築したシーンの読み込み、非同期の `getView()` カメラ取得往復、鎖見出しクリック用の `zoomTo({chain})`。 |
| `main.cpp` | エントリポイント: CLI引数解析（`--receptor`/`--poses`/`--reference`/`--manifest`）、`QApplication` セットアップ、ヘッドレス検証用の任意の `DD_CVIEW_SCREENSHOT` 環境変数フック（後述）。 |

`python/dd_cview_backend.py` がPython側の対応物——詳細は上の
[アーキテクチャ](#アーキテクチャ)節を参照。

## 設計上のポイント

- **計算ロジックはC++側で一切重複させない。** PDB/SDFのパース、距離計算、
  RDKit呼び出し、HTML生成のいずれも、`dd_viewer`/`dd_molview` 内部で
  Pythonのまま、無改変で実行される。`dd_cview` はpybind11境界越しに
  プリミティブ値を渡すだけ。
- **`PythonBridge` は `py::object` を一切外に漏らさない。** その公開API
  （`PythonBridge.h` を参照）はQt/std標準型のみなので、プロジェクト内の
  他のどのファイルもpybind11の存在を知る必要がなく、`Python.h`/
  `qobjectdefs.h` の `slots` 衝突が `PythonBridge.cpp` の外で起きる余地が
  ない。
- **PYTHONHOMEは明示的に設定し、`sys.path`のcwdエントリは除去する。**
  `dd_viewer`、`dd_molview`、`dd_cview` はすべて同じ親ディレクトリ
  （`~/work`）の下にきょうだいディレクトリとして存在する。もし `dd_cview`
  がその親ディレクトリをカレントディレクトリとして起動された場合、空文字列
  `""` の `sys.path` エントリが `import dd_viewer` を、そこに実在する
  `dd_viewer` という名前のディレクトリ（その階層に `__init__.py` がない）
  を起点にした壊れた*名前空間*パッケージへと——本物のeditableインストール
  済みパッケージの代わりに——静かに解決してしまう。例外を投げるのではなく
  `dd_viewer.__file__` が `None` になって返ってくるだけなので気づきにくい。
  `PythonBridge` のコンストラクタは何かをimportする前に空文字列/cwd由来の
  `sys.path` エントリを除去するため、バイナリをどこから起動してもこの問題
  は起こらない。
- **GILは一切解放しない。** すべての `PythonBridge` 呼び出しはQtのメイン
  スレッド上で同期的に行われ、元の `dd_molview-desktop` 自身のシングル
  スレッドQtイベントループモデルと一致している——バックグラウンドスレッドが
  インタプリタに触れることは一切ないため、`py::gil_scoped_release`/
  `acquire` はどこにも必要ない。

## サンプルデータ (`data/`)

`dd_molview` と同じSARS-CoV-2 Mproサンプル一式
（`6W63_receptor.pdb` / `6W63_redock.sdf` / `6W63_ligand_ref.sdf`、
`7L10.pdb` / `7L11.pdb`、`sample_manifest.json`）——各ファイルの用途は
[dd_molviewのREADME](../dd_molview/README.jp.md)を参照。

## 動作検証

`tests/bridge_test.cpp`（CTestの `bridge_smoke_test` として登録）は
`PythonBridge` 単体の統合スモークテストで、QtのGUIは一切関与しない:
同梱の6W63 + 7L10受容体と、6W63の再ドッキングポーズ9件+参照リガンドを
読み込み、`PythonBridge` の各メソッドがそれらに対して（テーブルの行/列数、
空でないシーンHTML/シーケンスHTML、正しいリンクスキーム、埋まった
コンタクトテーブル、実際のスコア/RMSD）実際に埋め込まれたインタプリタを
通してエンドツーエンドで妥当な値を返すことを確認する——
`ctest --test-dir build` で実行できる。

GUI全体はヘッドレス環境で検証済み（`QT_QPA_PLATFORM=offscreen`、
`DD_CVIEW_SCREENSHOT` 環境変数 / `QWidget::grab()` によるスクリーン
ショット取得——`dd_molview-desktop` 自身のテストスイートと同じ手法）:
同梱の6W63 + 7L10受容体、6W63のポーズ、参照リガンドを指定して起動すると
Proteins/Ligandsテーブルと Chains パネル（コンタクト残基の黄色ハイライト
含む）が正しく表示されること、引数なしで起動すると正しい空状態（空の
テーブル、無効化された「Show reference ligand」）になること、そして
`dd_viewer`/`dd_molview`/`dd_cview` が共有する親ディレクトリを作業
ディレクトリに設定して起動しても（上記の`sys.path`修正が対処する、まさに
そのシナリオ）データが正しく読み込まれることを確認済み。
`dd_molview-desktop` と同様、実際の `QWebEngineView` によるWebGL描画は
`QT_QPA_PLATFORM=offscreen` 下では観測できない——3Dビューの実際の画面
描画とカメラ保持挙動は実ディスプレイでの目視確認が必要。相互作用/カメラの
*ロジック*自体は `dd_viewer`/`dd_molview` 自身のテストスイートで
（無改変のまま）検証されており、本プロジェクトではそれを重複して検証
していない。

## ライセンス

MIT — [LICENSE](LICENSE) を参照。
