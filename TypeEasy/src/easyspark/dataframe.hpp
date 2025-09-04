#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

// Tipo para representar una fila: columna → valor
using Row = std::unordered_map<std::string, std::string>;

class DataFrame {
public:
    std::vector<Row> rows;
    std::vector<std::string> columns;

    // Filtro por lambda
    DataFrame filter(std::function<bool(const Row&)> predicate) const;

    // Selección de columnas
    DataFrame select(const std::vector<std::string>& selected_columns) const;

    // Mostrar N primeras filas
    void show(int limit = 10) const;
};

// Carga un CSV y devuelve un DataFrame
DataFrame read(const std::string& filename);
