# Djit+ Vector Clock

Djit+ のアルゴリズムに基づいて実装したベクタークロックの実験プログラムです。こ
のソフトウェアが解析の対象とするのは現実のプログラムではなく，関数呼び出しの列
として表現したモデルです。

Djit+ のアルゴリズムやベクタークロックの仕組みについて詳しくは
[ベクタークロックと競合検査](https://uchan.hateblo.jp/entry/2020/05/12/185631)
を参照してください。

## モデルの記述

検査対象のモデルは `Analyzer` クラスの `Read` や `Acquire` などのメソッドを呼ぶ
ことで記述します。競合が検出されるモデルの実装例を次に示します。

    #include <iostream>
    #include "fixed.hpp"

    int main() {
      // 解析器の準備
      Analyzer<2> a;
      a.SetReadViolationHandler(
        [](const auto& an, int t, const auto& x) {
          std::cout << "race condition detected: rd("
                    << t << "," << x.name << ")" << std::endl;
        });
      a.SetWriteViolationHandler(
        [](const auto& an, int t, const auto& x) {
          std::cout << "race condition detected: wr("
                    << t << "," << x.name << ")" << std::endl;
        });

      // 検査対象のモデル
      Variable x{"x"};
      a.Register(x);

      a.Read(0, x);
      a.Read(1, x);
      a.Write(0, x);
      a.Write(1, x);
    }

これを main.cpp として保存してビルド，実行すると次の結果を得られます。
`a.Write(0, x)` と `a.Write(1, x)` で競合状態が検出されたことを意味します。

    $ ./analyzer
    race condition detected: wr(0,x)
    race condition detected: wr(1,x)

main.cpp には最初から競合が検出されるモデルを記述してあります。また，
`PROTECT_BY_LOCK` マクロを定義してビルドすることで，ロックを使って競合状態を防
いだモデルを試すことができます。

## ビルド

    $ make

コンパイラを変えるときは CXX 変数をセットします。

    $ CXX=clang++-8 make

## 実行

    $ ./analyzer
