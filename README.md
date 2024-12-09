# Grid Trading Bot

## Dependencies

### Required Libraries
- nlohmann-json (JSON for Modern C++)
- libcurl (HTTP requests)
- gnuplot (optional, for chart generation)

### Installation (macOS)

使用 Homebrew 安装所需依赖：

```
brew install nlohmann-json libcurl gnuplot
```

### Configuration

在运行程序前，请确保：
1. 创建并配置 `config.json` 文件
2. 确保所有依赖库都已正确安装
3. 编译器支持 C++17 或更高版本

### Build

使用 clang++ 编译

```
clang++ -std=c++17 main.cpp -lcurl -o grid_trading
```
