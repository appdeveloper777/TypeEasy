#include "dataframe.hpp"
#include <fstream>
#include <sstream>
#include <iostream>

DataFrame read(const std::string& filename) {
    DataFrame df;
    std::ifstream file(filename);
    std::string line;

    if (file.is_open()) {
        // Leer encabezados
        if (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string col;
            while (std::getline(ss, col, ',')) {
                df.columns.push_back(col);
            }
        }

        // Leer filas
            // Leer filas
        while (std::getline(file, line)) {
            std::stringstream ss(line);
            std::string val;
            Row row;
            size_t i = 0;

            while (std::getline(ss, val, ',')) {
                if (i < df.columns.size()) {
                    row[df.columns[i]] = val;
                    ++i;
                } else {
                    break;
                }
            }

            // Solo agregar filas que tengan al menos una columna válida
            if (!row.empty()) {
                df.rows.push_back(row);
            }
        }

    } else {
        std::cerr << " No se pudo abrir el archivo: " << filename << std::endl;
    }

    return df;
}

DataFrame DataFrame::filter(std::function<bool(const Row&)> predicate) const {
    DataFrame result;
    result.columns = this->columns;

    for (const auto& row : rows) {
        if (predicate(row)) {
            result.rows.push_back(row);
        }
    }

    return result;
}

DataFrame DataFrame::select(const std::vector<std::string>& selected_columns) const {
    DataFrame result;
    result.columns = selected_columns;

    for (const auto& row : rows) {
        Row new_row;
        for (const auto& col : selected_columns) {
            new_row[col] = row.at(col);
        }
        result.rows.push_back(new_row);
    }

    return result;
}

void DataFrame::show(int limit) const {
    for (const auto& col : columns) {
        std::cout << col << "\t";
    }
    std::cout << "\n";

    int count = 0;
    for (const auto& row : rows) {
        for (const auto& col : columns) {
            auto it = row.find(col);
            if (it != row.end()) {
                std::cout << it->second << "\t";
            } else {
                std::cout << "(null)\t";
            }
        }
        std::cout << "\n";
        if (++count >= limit) break;
    }
}

