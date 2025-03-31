#include "easyspark/dataframe.hpp"
#include <chrono>
#include <iostream>
int main() {
    auto start = std::chrono::high_resolution_clock::now();
    DataFrame df = read("dataset2.csv");
    auto filtered = df.filter([](const Row& row) {
        auto it = row.find("adult");
        return it != row.end() && it->second == "FALSE";
    });
    std::cout << "Filas que cumplen el filtro: " << filtered.rows.size() << std::endl;
    filtered.select({"title"}).show();
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Duración: " << duration.count() << " ms" << std::endl;
    return 0;
}

