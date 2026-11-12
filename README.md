# HU AoF CFR

note連載『ポーカーソルバーのブラックボックスをのぞく』第8回の教育・研究用ソルバーです。Heads-Up No-Limit Hold’emのAll-in or Foldを、通常のCFRで解きます。計算はC++、操作とCSV出力はPythonで行います。

## インストールと実行

Python 3.10以上、C++17対応コンパイラ、CMake 3.18以上が必要です。このリポジトリのルートで実行します。

```sh
python -m venv .venv
source .venv/bin/activate
python -m pip install -e ".[test,plot]"
python examples/run.py --iterations 100000 --evaluate-every 1000
python examples/visualize.py --per-100
python -m pytest -q
```

上記の仮想環境の有効化はLinux・macOS・WSL用です。Windows PowerShellでは `.venv\Scripts\Activate.ps1` を使い、Visual Studio Build ToolsのC++ツールチェーンを用意してください。

```python
from hu_aof_cfr import CFRSolver

solver = CFRSolver()
solver.train(iterations=100_000, evaluate_every=1_000)

strategy = solver.average_strategy()
response = solver.best_response()
metrics = solver.evaluate()
print(metrics["nash_conv"], metrics["exploitability"])
print(solver.evaluate(per_100=True)["exploitability"])

print(solver.save_run("results"))
```

`train` を再度呼ぶと同じインスタンスに反復を追加します。収束履歴の先頭には反復0（学習前の一様戦略）が入り、以降は累積反復数が指定間隔の倍数になった時点で記録します。最初の10反復を毎回記録するには `python examples/run.py --iterations 10 --evaluate-every 1` を使います。最後の反復が倍数でない場合も `evaluate()` でその時点の平均戦略を評価できます。

`examples/run.py` と `save_run()` は保存するたびに `results/run_YYYY-MM-DD_HHMMSS_ffffff_JST/` を作成し、CSVとメタデータをまとめて保存します。`--output` で親フォルダーを変更できます。`examples/visualize.py` は引数なしなら `results/` の最新の実行結果を読み、同じフォルダーにグラフを保存します。別の実行を描くにはその `convergence.csv` のパスを渡します。

グラフは横軸が反復0から始まる線形軸で、縦軸は底10の対数軸です。縦の目盛りは指数で表示します。たとえば −3 は元の指標の 0.001 に相当します。縦軸の下端は正のデータの最小値に合わせた10の累乗です。0以下の値はCSVに残し、グラフでは該当点を空けます。

## ゲームと計算

両者の開始スタックは8BB、SBは0.5BB、BBは1BBです。アンティ、レーキ、賞金はありません。SBはFoldかAll-inを選び、All-inに対してのみBBがFoldかCallを選びます。利得はブラインドを払う前からの純増減です。

| 終端 | SB | BB |
|---|---:|---:|
| SB Fold | −0.5 | +0.5 |
| SB All-in → BB Fold | +1 | −1 |
| SB All-in → BB Call | 16 × SBのEquity − 8 | 16 × BBのEquity − 8 |

両席169クラスずつの338情報セットを一様戦略で初期化します。各反復で同じ戦略を使って両席の反現実効用を計算し、符号付き累積後悔を更新します。正の累積後悔に比例して次の行動確率を決め、正の値がなければ一様に戻します。平均戦略は各反復の更新前の戦略の算術平均です。両者とも自分の意思決定は一度なので、先行する自分の行動の到達確率は1です。

169クラス表現は、具体的な配札をスートの一括置換でまとめたものです。各相対スート配置の出現数と正確なショーダウン件数を集計し、**同時配札確率**と**配札確率を掛けたショーダウン利得**を保存します。相手のクラスはカード重複を除いた同時分布で扱います。ボードに依存する意思決定がないため、全ボードの利得は先に期待値にまとめられます。

## 配布するEquityデータ

`hu_aof_cfr/data/hu_equity.bin` は、既存のNinja_solverが52枚のデッキから全ボードを列挙して作成したHUデータを再梱包したものです。元データは `deals/ordered_deals.csv` の整数カウントで、作成時のOMPEvalはリビジョン `4aec210ff75b0851af0ee170b35a7899e1a4fe8f` です。配布パッケージはこのデータを読み込むため、Ninja_solverやOMPEvalをインストールする必要はありません。

- 形式はリトルエンディアン、総サイズは **1,125,248バイト**。
- 20バイトのヘッダーは8バイトのASCII識別子 `HUAOFEQ1` と、32ビット符号なし整数のレコード数（93,769）、各配札のボード数（1,712,304）、合法な席順付き配札数（1,624,350）。
- 各12バイトのレコードは32ビット符号なし整数のキー、SBの勝ち数、引き分け数。負け数はボード数から勝ち数と引き分け数を引いて得る。Equityは `(勝ち数 + 引き分け数 / 2) / ボード数`。
- カードIDは `4 * rank + suit`。ランク順は `23456789TJQKA`、スート順は `shcd`。各席の2枚を昇順にし、SB→BBの順に各6ビットで連結する。スートの24置換における最小のキーを使用し、席順は保持する。
- SHA-256は `7753a849640a97521e573d744bf36455c4d3eef31a2575b7ae38c9c71af5d733`。

PythonでSHA-256を照合し、C++でヘッダー、件数、キー、カウント範囲、席交換対称性を検証します。異なる具体的配札の個数をスート置換から復元して重みとし、全配札数と各クラスの周辺配札数を照合します。テストではさらに全1,624,350具体的配札を展開し、非一様な戦略で両席の全情報セットの行動価値を比較します。全ボードの再列挙を必要としない照合です。

## 評価とファイル

最適反応は、相手の平均戦略を固定し、各自のハンドクラスで期待値の高い行動を選びます。`best_response()` のSBとBBは、それぞれ相手への片側変更として評価します。両者の改善幅の和がNashConv、その半分がExploitabilityです。単位はBB/handで、`evaluate(per_100=True)` はBB/100 handsを返します。

- `strategy.csv` は平均戦略338行。列は `position,hand,fold,allin,call`。該当しない行動列は空欄。
- `convergence.csv` は平均戦略の記録。列は `iteration,nash_conv,exploitability,sb_ev,bb_ev`。
- `metadata.json` はゲーム条件、累積反復数、更新・平均方式、単位、Equityの形式とハッシュ。各CSVの保存先と同じディレクトリに出力する。同じ時点を比較するときは、学習を挟まずに両CSVを保存する。

出力先ディレクトリは必要に応じて作成します。指定したファイル名に既存ファイルがあれば置き換えます。反復回数が有限のため、出力される平均戦略は均衡の近似です。計算には倍精度浮動小数点を使用します。対応ゲームは上記の固定ルールです。
