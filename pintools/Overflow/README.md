## Overflow detector

malloc() で生成した領域に対するバッファオーバーフローを検出します。

[Intel Pin](https://software.intel.com/content/www/us/en/develop/articles/pin-a-dynamic-binary-instrumentation-tool.html)
を用いて実装してあります。

## Build

    make PIN_ROOT=/path/to/intel-pin
