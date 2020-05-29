## VectorClock detector

マルチスレッドの競合状態（race condition）を検出します

[Intel Pin](https://software.intel.com/content/www/us/en/develop/articles/pin-a-dynamic-binary-instrumentation-tool.html)
を用いて実装してあります。

## Build

    export PIN_ROOT=/path/to/intel-pin

    cd VectorClock
    make

    cd target
    make
    make run
