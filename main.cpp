#include <iostream>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <cmath>
#include <map>
#include <vector>
#include <cstdlib>  // 用於 system 函數

using json = nlohmann::json;

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, std::string *output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}


/**
 * @brief 從Binance獲取指定交易對的當前價格
 * @param symbol 交易對名稱（例如："ETHUSDT", "BTCUSDT"）
 * @return 當前價格
 */
double getCurrentPrice(const std::string& symbol) {
    CURL *curl;
    CURLcode res;
    std::string readBuffer;

    curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("cURL init failed");
    }

    std::string url = "https://api.binance.com/api/v3/ticker/price?symbol=" + symbol;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        std::cerr << "cURL Error: " << std::to_string(res) << std::endl;
        throw std::runtime_error("cURL Error: " + std::to_string(res));
    }

    try {
        json response = json::parse(readBuffer);
        std::string priceStr = response["price"].get<std::string>();
        std::cout << "Current price fetched: " << priceStr << std::endl;
        return std::stod(priceStr);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("JSON parse error: " + std::string(e.what()));
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error("Invalid argument in JSON response: " + std::string(e.what()));
    }
}

void placeOrder(const std::string& side, double quantity, double price) {
    std::cout << "Placing " << side << " order for " << quantity << " ETH at price " << price << std::endl;
}

double calculateDynamicGridSpacing(double volatility) {
    // 根据市场波动性计算动态网格间距
    return std::max(0.5, volatility * 0.01); // 示例式
}

// 訂單結構體
struct Order {
    std::string orderId;
    std::string side;  // "buy" or "sell"
    double price;
    double quantity;
    double gridLevel;  // 對應的網格線價格
    bool isOpen;       // 訂單是否仍然有
    
    Order(const std::string& id, const std::string& s, double p, double q, double g)
        : orderId(id), side(s), price(p), quantity(q), gridLevel(g), isOpen(true) {}
};

// 倉位信息結構體
struct Position {
    double quantity;      // 當前持倉數量
    double avgPrice;      // 平均持倉價格
    double totalCost;     // 總成本
    double unrealizedPnL; // 未實現盈虧
    
    Position() : quantity(0), avgPrice(0), totalCost(0), unrealizedPnL(0) {}
};

// 風險管理類
class RiskManager {
private:
    double maxPositionSize;    // 最大持倉數量
    double maxDrawdown;        // 最大回撤限制
    double initialEquity;      // 初始資金
    double currentEquity;      // 當前資金
    double maxLossPerTrade;    // 單筆最大虧損限制
    
public:
    RiskManager(double initialEquity, double maxPositionSize, double maxDrawdownPercent, double maxLossPercent)
        : initialEquity(initialEquity)
        , currentEquity(initialEquity)
        , maxPositionSize(maxPositionSize)
        , maxDrawdown(initialEquity * maxDrawdownPercent)
        , maxLossPerTrade(initialEquity * maxLossPercent) {}
    
    bool canPlaceOrder(const std::string& side, double quantity, double price) {
        // 檢查持倉限制
        if (quantity > maxPositionSize) {
            std::cout << "Order rejected: Exceeds maximum position size" << std::endl;
            return false;
        }
        
        // 檢查資金是否足夠
        double orderCost = quantity * price;
        if (orderCost > currentEquity) {
            std::cout << "Order rejected: Insufficient funds" << std::endl;
            return false;
        }
        
        return true;
    }
    
    void updateEquity(double pnl) {
        currentEquity += pnl;
        double drawdown = initialEquity - currentEquity;
        
        if (drawdown > maxDrawdown) {
            std::cout << "WARNING: Maximum drawdown exceeded!" << std::endl;
        }
    }
    
    double getCurrentEquity() const { return currentEquity; }
};

// 網格訂單管理類
class GridOrderManager {
private:
    std::map<double, std::vector<Order>> gridOrders;  // 每個網格線對應的訂單
    double gridSpacing;
    double minOrderQuantity;
    Position position;
    RiskManager riskManager;
    std::map<std::string, double> realizedPnL;  // 已實現盈虧記錄
    std::ofstream logFile;  // 日誌文件
    const json& config;  // 存儲配置引用
    
public:
    GridOrderManager(const json& cfg) 
        : config(cfg)
        , gridSpacing(cfg["grid_spacing"])
        , minOrderQuantity(cfg["min_order_quantity"])
        , riskManager(
            cfg["initial_investment"],
            cfg["max_position_size"],
            cfg["max_drawdown_percent"],
            cfg["max_loss_per_trade_percent"]
          ) {
        // 初始化日誌
        logFile.open(config["log_file_path"].get<std::string>(), std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Failed to open log file!" << std::endl;
        }
    }
    
    ~GridOrderManager() {
        if (logFile.is_open()) {
            logFile.close();
        }
    }
    
    // 添加新訂單
    bool addOrder(const std::string& side, double price, double gridLevel) {
        // 檢查風險限制
        if (!riskManager.canPlaceOrder(side, minOrderQuantity, price)) {
            return false;
        }
        
        std::string orderId = generateOrderId();
        Order order(orderId, side, price, minOrderQuantity, gridLevel);
        gridOrders[gridLevel].push_back(order);
        
        // 更新倉位
        if (side == "buy") {
            updatePosition(minOrderQuantity, price, true);
        } else {
            updatePosition(minOrderQuantity, price, false);
        }
        
        std::cout << "New " << side << " order placed at grid level " << gridLevel 
                 << " (Price: " << price << ")" << std::endl;
        
        // 記錄日誌
        if (logFile.is_open()) {
            logFile << "New " << side << " order placed at grid level " << gridLevel 
                    << " (Price: " << price << ", Quantity: " << minOrderQuantity << ")\n";
        }
        
        return true;
    }
    
    // 更新網格系統
    void updateGrids(double currentPrice, const std::vector<double>& newGridLevels) {
        // 關閉超出新網格範圍的訂單
        auto it = gridOrders.begin();
        while (it != gridOrders.end()) {
            if (std::find(newGridLevels.begin(), newGridLevels.end(), it->first) == newGridLevels.end()) {
                closeOrdersAtGrid(it->first);
                it = gridOrders.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // 檢查是否需要在特定網格線開立新訂單
    bool shouldPlaceOrderAtGrid(double gridLevel, const std::string& side) {
        auto it = gridOrders.find(gridLevel);
        if (it == gridOrders.end()) return true;
        
        // 檢查是否已有相同方向的活躍訂單
        for (const auto& order : it->second) {
            if (order.isOpen && order.side == side) {
                return false;
            }
        }
        return true;
    }
    
    // 打���當前活躍訂單
    void printActiveOrders() const {
        std::cout << "\nActive Orders:" << std::endl;
        for (const auto& [grid, orders] : gridOrders) {
            for (const auto& order : orders) {
                if (order.isOpen) {
                    std::cout << "Grid " << grid << ": " 
                            << order.side << " order at " << order.price 
                            << " (Quantity: " << order.quantity << ")" << std::endl;
                }
            }
        }
    }
    
    // 更新倉位信息
    void updatePosition(double quantity, double price, bool isBuy) {
        if (isBuy) {
            double newQuantity = position.quantity + quantity;
            position.avgPrice = (position.quantity * position.avgPrice + quantity * price) / newQuantity;
            position.quantity = newQuantity;
            position.totalCost += quantity * price;
        } else {
            position.quantity -= quantity;
            // 計算已實現盈虧
            double pnl = (price - position.avgPrice) * quantity;
            realizedPnL[generateOrderId()] = pnl;
            riskManager.updateEquity(pnl);
        }
        
        // 記錄日誌
        if (logFile.is_open()) {
            logFile << (isBuy ? "Buy" : "Sell") << " executed: " 
                    << "Price: " << price << ", Quantity: " << quantity 
                    << ", PnL: " << (isBuy ? 0 : (price - position.avgPrice) * quantity) << "\n";
        }
    }
    
    // 打印交易統計
    void printTradingStats(double currentPrice) const {
        std::cout << "\n=== Trading Statistics ===" << std::endl;
        std::cout << "Current Position:" << std::endl;
        std::cout << "Quantity: " << position.quantity << std::endl;
        std::cout << "Average Price: " << position.avgPrice << std::endl;
        
        // 計算未實現盈虧
        double unrealizedPnL = position.quantity * (currentPrice - position.avgPrice);
        std::cout << "Unrealized P&L: " << unrealizedPnL << std::endl;
        
        // 計算總實現盈虧
        double totalRealizedPnL = 0;
        for (const auto& [orderId, pnl] : realizedPnL) {
            totalRealizedPnL += pnl;
        }
        std::cout << "Total Realized P&L: " << totalRealizedPnL << std::endl;
        
        // 顯示當前資金
        std::cout << "Current Equity: " << riskManager.getCurrentEquity() << std::endl;
        
        // 注释掉图表生成
        // generateChart();
    }
    
    // 添加生成圖表的方法
    void generateChart() const {
        std::ofstream dataFile(config["data_file_path"].get<std::string>());
        if (!dataFile.is_open()) {
            std::cerr << "Failed to open data file!" << std::endl;
            return;
        }
        
        // 寫入數據
        for (const auto& [grid, orders] : gridOrders) {
            for (const auto& order : orders) {
                if (order.isOpen) {
                    dataFile << grid << " " << order.price << " " << order.quantity << "\n";
                }
            }
        }
        dataFile.close();
        
        std::string command = "gnuplot -e \"set terminal png; set output '" + 
            config["chart_output_path"].get<std::string>() + 
            "'; plot '" + config["data_file_path"].get<std::string>() + 
            "' using 1:2 with linespoints\"";
        system(command.c_str());
    }
    
private:
    // 生成唯一訂單ID
    std::string generateOrderId() {
        static int orderCounter = 0;
        return "ORDER_" + std::to_string(++orderCounter);
    }
    
    // 關閉特定網格線上的所有訂單
    void closeOrdersAtGrid(double gridLevel) {
        auto it = gridOrders.find(gridLevel);
        if (it != gridOrders.end()) {
            for (auto& order : it->second) {
                if (order.isOpen) {
                    order.isOpen = false;
                    std::cout << "Closing order " << order.orderId 
                             << " at grid level " << gridLevel << std::endl;
                }
            }
        }
    }
};

// 修改 gridTrading 函數
void gridTrading(const json& config) {
    static GridOrderManager orderManager(config);
    
    double currentPrice = getCurrentPrice(config["trading_pair"]);
    const int GRID_COUNT = config["grid_count"];
    double gridSpacing = config["grid_spacing"];
    
    // 計算網格線
    double baseGrid = std::round(currentPrice / gridSpacing) * gridSpacing;
    std::vector<double> gridLevels;
    for (int i = -GRID_COUNT; i <= GRID_COUNT; i++) {
        gridLevels.push_back(baseGrid + (i * gridSpacing));
    }
    
    // 更新訂單管理系統
    orderManager.updateGrids(currentPrice, gridLevels);
    
    // 檢查每個網格線是否需要開立新訂單
    for (size_t i = 0; i < gridLevels.size() - 1; i++) {
        double lowerGrid = gridLevels[i];
        double upperGrid = gridLevels[i + 1];
        
        if (currentPrice > lowerGrid && currentPrice <= upperGrid) {
            // 檢查是否需要在下方網格線買入
            if (std::abs(currentPrice - lowerGrid) < gridSpacing * 0.1) {
                if (orderManager.shouldPlaceOrderAtGrid(lowerGrid, "buy")) {
                    orderManager.addOrder("buy", currentPrice, lowerGrid);
                }
            }
            // 檢查是否需要在上方網格線賣出
            else if (std::abs(currentPrice - upperGrid) < gridSpacing * 0.1) {
                if (orderManager.shouldPlaceOrderAtGrid(upperGrid, "sell")) {
                    orderManager.addOrder("sell", currentPrice, upperGrid);
                }
            }
        }
    }
    
    // 打印當前狀態
    std::cout << "\nCurrent price: " << currentPrice << std::endl;
    std::cout << "Base grid: " << baseGrid << std::endl;
    orderManager.printActiveOrders();
    
    // 在循環結束時添加統計信息打印
    orderManager.printTradingStats(currentPrice);
    
    // 使用配置的更新間隔
    std::this_thread::sleep_for(
        std::chrono::seconds(config["update_interval_seconds"]));
}

int main() {
    std::cout << "Reading configuration file..." << std::endl;
    std::ifstream configFile("config.json");
    json config;
    configFile >> config;

    std::cout << "Configuration loaded. Starting trading for " 
              << config["trading_pair"] << "..." << std::endl;
    std::cout << "Grid mode: " 
              << (config["infinite_grid"] ? "Infinite" : "Limited") << std::endl;

    while (true) {
        try {
            gridTrading(config);
        } catch (const std::runtime_error& error) {
            std::cerr << "Error: " << error.what() << std::endl;
        }
    }

    return 0;
}
