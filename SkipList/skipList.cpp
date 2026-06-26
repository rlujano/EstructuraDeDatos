#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <random>
#include <algorithm>

// ==========================================
// 1. ESTRUCTURA DE DATOS PARA LA TARJETA
// ==========================================
struct Tarjeta {
    int id;
    int client_id;
    std::string card_brand;
    std::string card_type;
    std::string card_number;
    std::string expires;
    int cvv;
    std::string has_chip;
    int num_cards_issued;
    double credit_limit;
};

// ==========================================
// 2. CLASE NODO
// ==========================================
class NodoSkipList {
public:
    int clave; 
    Tarjeta tarjeta;
    std::vector<NodoSkipList*> forward;

    NodoSkipList(int clv, Tarjeta tarj, int nivel) {
        this->clave = clv;
        this->tarjeta = tarj;
        this->forward.assign(nivel + 1, nullptr);
    }
};

// ==========================================
// 3. CLASE PRINCIPAL: SKIP LIST
// ==========================================
class SkipList {
private:
    int MAX_LEVEL;
    float P;
    int nivel_actual;
    NodoSkipList* head;

public:
    int generarNivelAleatorio() {
        int lvl = 0;
        while (((double)rand() / RAND_MAX) < P && lvl < MAX_LEVEL) {
            lvl++;
        }
        return lvl;
    }

    SkipList(int max_lvl, float p) {
        this->MAX_LEVEL = max_lvl;
        this->P = p;
        this->nivel_actual = 0;
        Tarjeta tarj_vacia = {-1, -1, "", "", "", "", -1, "", -1, 0.0};
        this->head = new NodoSkipList(-1, tarj_vacia, MAX_LEVEL);
    }

    ~SkipList() {
        NodoSkipList* actual = head;
        while (actual != nullptr) {
            NodoSkipList* siguiente = actual->forward[0];
            delete actual;
            actual = siguiente;
        }
        std::cout << ">> Memoria RAM liberada correctamente.\n";
    }

    void insertarConNivel(int clave, Tarjeta tarj, int nivel_aleatorio) {
        NodoSkipList* actual = head;
        std::vector<NodoSkipList*> update(MAX_LEVEL + 1, nullptr);

        for (int i = nivel_actual; i >= 0; i--) {
            while (actual->forward[i] != nullptr && actual->forward[i]->clave < clave) {
                actual = actual->forward[i];
            }
            update[i] = actual;
        }

        actual = actual->forward[0];

        if (actual != nullptr && actual->clave == clave) {
            actual->tarjeta = tarj;
            return;
        }

        if (nivel_aleatorio > nivel_actual) {
            for (int i = nivel_actual + 1; i <= nivel_aleatorio; i++) {
                update[i] = head;
            }
            nivel_actual = nivel_aleatorio;
        }

        NodoSkipList* nuevo_nodo = new NodoSkipList(clave, tarj, nivel_aleatorio);

        for (int i = 0; i <= nivel_aleatorio; i++) {
            nuevo_nodo->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = nuevo_nodo;
        }
    }

    NodoSkipList* buscarPuro(int clave) {
        NodoSkipList* actual = head;
        for (int i = nivel_actual; i >= 0; i--) {
            while (actual->forward[i] != nullptr && actual->forward[i]->clave < clave) {
                actual = actual->forward[i];
            }
        }
        actual = actual->forward[0];
        if (actual != nullptr && actual->clave == clave) {
            return actual;
        }
        return nullptr;
    }

    // ==========================================================
    // NUEVAS FUNCIONES DE AUDITORÍA PEDIDAS POR EL USUARIO
    // ==========================================================

    // 1. Cuenta cuántos elementos reales tiene la estructura recorriendo el nivel base
    int contarElementos() {
        int contador = 0;
        NodoSkipList* actual = head->forward[0]; // Empezamos en el primer nodo real
        while (actual != nullptr) {
            contador++;
            actual = actual->forward[0]; // Avanzar por el nivel 0
        }
        return contador;
    }

    // Helper para imprimir de forma tabular los datos de una tarjeta
    void imprimirFormatoTarjeta(NodoSkipList* nodo) {
        if (!nodo) return;
        std::cout << "ID: " << std::setw(6) << nodo->tarjeta.id 
                  << " | Cliente: " << std::setw(6) << nodo->tarjeta.client_id 
                  << " | " << std::setw(10) << nodo->tarjeta.card_brand 
                  << " | " << std::setw(12) << nodo->tarjeta.card_type 
                  << " | Límite: $" << std::fixed << std::setprecision(2) << nodo->tarjeta.credit_limit << "\n";
    }

    // 2. Muestra los primeros N elementos (los IDs más bajos)
    void mostrarPrimeros(int n) {
        std::cout << "\n--- MOSTRANDO LOS PRIMEROS " << n << " ELEMENTOS (ORDEN ASCENDENTE) ---\n";
        NodoSkipList* actual = head->forward[0];
        int impresos = 0;
        
        while (actual != nullptr && impresos < n) {
            imprimirFormatoTarjeta(actual);
            impresos++;
            actual = actual->forward[0];
        }
        if (impresos == 0) std::cout << "La estructura está vacía.\n";
    }

    // 3. Muestra los últimos N elementos (los IDs más altos)
    void mostrarUltimos(int n) {
        std::cout << "\n--- MOSTRANDO LOS ULTIMOS " << n << " ELEMENTOS (ORDEN ASCENDENTE) ---\n";
        NodoSkipList* actual = head->forward[0];
        std::vector<NodoSkipList*> ultimos_n;

        // Recorremos toda la lista manteniendo únicamente los últimos N en un buffer circular/vector
        while (actual != nullptr) {
            ultimos_n.push_back(actual);
            if (ultimos_n.size() > (size_t)n) {
                ultimos_n.erase(ultimos_n.begin()); // Quitamos el más viejo para mantener solo N
            }
            actual = actual->forward[0];
        }

        // Imprimir los recolectados
        for (NodoSkipList* nodo : ultimos_n) {
            imprimirFormatoTarjeta(nodo);
        }
        if (ultimos_n.empty()) std::cout << "La estructura está vacía.\n";
    }
};

// ==========================================
// 4. PARSER Y LECTOR DE CSV FINANCIERO
// ==========================================
void cargarDatasetDesdeCSV(SkipList& db, const std::string& ruta_archivo, int max_niveles) {
    std::ifstream archivo(ruta_archivo);
    if (!archivo.is_open()) {
        std::cout << "[ERROR] No se pudo abrir el archivo CSV en la ruta: " << ruta_archivo << "\n";
        return;
    }

    std::vector<int> contador_carriles(max_niveles + 1, 0);
    auto start_time = std::chrono::high_resolution_clock::now();

    std::string linea;
    std::getline(archivo, linea); // Ignorar cabecera

    int contador_insertados = 0;

    while (std::getline(archivo, linea)) {
        if (linea.empty() || (linea.find("Credit") != std::string::npos && linea.find(",") == std::string::npos)) continue;

        std::stringstream flujo_linea(linea);
        std::string c_id, c_client, c_brand, c_type, c_num, c_exp, c_cvv, c_chip, c_issued, c_limit;

        if (std::getline(flujo_linea, c_id, ',') && std::getline(flujo_linea, c_client, ',') &&
            std::getline(flujo_linea, c_brand, ',') && std::getline(flujo_linea, c_type, ',') &&
            std::getline(flujo_linea, c_num, ',') && std::getline(flujo_linea, c_exp, ',') &&
            std::getline(flujo_linea, c_cvv, ',') && std::getline(flujo_linea, c_chip, ',') &&
            std::getline(flujo_linea, c_issued, ',') && std::getline(flujo_linea, c_limit, ',')) {
            
            try {
                if (!c_limit.empty() && c_limit[0] == '$') {
                    c_limit = c_limit.substr(1);
                }

                Tarjeta t;
                t.id = std::stoi(c_id);
                t.client_id = std::stoi(c_client);
                t.card_brand = c_brand;
                t.card_type = c_type;
                t.card_number = c_num;
                t.expires = c_exp;
                t.cvv = std::stoi(c_cvv);
                t.has_chip = c_chip;
                t.num_cards_issued = std::stoi(c_issued);
                t.credit_limit = std::stod(c_limit);
                
                int nivel_nodo = db.generarNivelAleatorio();
                contador_carriles[nivel_nodo]++;
                
                db.insertarConNivel(t.id, t, nivel_nodo);
                contador_insertados++;
            } catch (...) {
                continue; 
            }
        }
    }
    archivo.close();

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "==================================================\n";
    std::cout << "[LOG METRICAS DE CONSTRUCCION]\n";
    std::cout << " -> Tiempo de carga en memoria RAM: " << duration.count() << " ms.\n";
    std::cout << " -> Total registros financieros indexados: " << contador_insertados << "\n";
    std::cout << "==================================================\n";
}

// ==========================================
// 5. MENÚ E INTERFAZ DE USUARIO AMPLIADA
// ==========================================
static std::vector<int> generarConsultasHotset(int total_queries, int max_id) {
    // Hotset fijo: IDs 1..10 concentran 80% de consultas
    std::mt19937 rng(123);
    std::uniform_real_distribution<double> prob(0.0, 1.0);
    std::uniform_int_distribution<int> hot(1, 10);
    std::uniform_int_distribution<int> cold(0, max_id);

    std::vector<int> ids;
    ids.reserve((size_t)total_queries);
    for (int i = 0; i < total_queries; i++) {
        if (prob(rng) < 0.80) ids.push_back(hot(rng));
        else ids.push_back(cold(rng));
    }
    return ids;
}

static void benchmarkHotset(SkipList& sl, int total_queries, int max_id, int repeticiones = 7) {
    auto ids = generarConsultasHotset(total_queries, max_id);

    std::vector<long long> totals;
    totals.reserve((size_t)repeticiones);

    int found = 0;

    for (int r = 0; r < repeticiones; r++) {
        // Warm-up para estabilizar cachés (por repetición)
        for (int i = 0; i < std::min(10000, total_queries); i++) {
            sl.buscarPuro(ids[i]);
        }

        auto t0 = std::chrono::high_resolution_clock::now();
        found = 0;
        for (int i = 0; i < total_queries; i++) {
            if (sl.buscarPuro(ids[i]) != nullptr) found++;
        }
        auto t1 = std::chrono::high_resolution_clock::now();

        auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        totals.push_back(total_ns);
    }

    std::sort(totals.begin(), totals.end());
    long long median_total = totals[totals.size() / 2];
    long long min_total = totals.front();
    long long max_total = totals.back();

    double median_avg = (double)median_total / (double)total_queries;

    std::cout << "\n[BENCHMARK] reps=" << repeticiones
              << " | queries=" << total_queries
              << " | found=" << found
              << " | median_total=" << median_total << " ns"
              << " | median_avg=" << (long long)median_avg << " ns"
              << " | min_total=" << min_total << " ns"
              << " | max_total=" << max_total << " ns\n";
}

int main() {
    srand(time(nullptr));

    int max_niveles_global = 16;
    SkipList estructura_base(max_niveles_global, 0.5);

    std::cout << "--- ENTORNO DE EVALUACION: ESTRUCTURA TRADICIONAL ---\n";
    std::cout << "Cargando archivo 'cards_data.csv'...\n";
    cargarDatasetDesdeCSV(estructura_base, "../Data/cards_data.csv", max_niveles_global);

    int opcion = 0;
    while (opcion != 6) {
        std::cout << "\n========================================\n";
        std::cout << "Menu de Opciones (Auditoria):\n";
        std::cout << "1. Buscar tarjeta bancaria por ID\n";
        std::cout << "2. Ver TOTAL de elementos en la Skip List\n";
        std::cout << "3. Ver los N PRIMEROS elementos\n";
        std::cout << "4. Ver los N ULTIMOS elementos\n";
        std::cout << "5. Benchmark (tiempo total / promedio)\n";
        std::cout << "6. Salir\n";
        std::cout << "Selecciona una opcion: ";
        std::cin >> opcion;

        if (opcion == 1) {
            int id_buscado;
            std::cout << "Ingresa el ID de la tarjeta a buscar: ";
            std::cin >> id_buscado;

            auto start_search = std::chrono::high_resolution_clock::now();
            NodoSkipList* resultado = estructura_base.buscarPuro(id_buscado);
            auto end_search = std::chrono::high_resolution_clock::now();
            auto duration_search_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_search - start_search);

            if (resultado != nullptr) {
                std::cout << "\n[TARJETA ENCONTRADA]\n";
                estructura_base.imprimirFormatoTarjeta(resultado);
            } else {
                std::cout << "\nLa tarjeta con ID " << id_buscado << " NO existe.\n";
            }
            std::cout << ">> Tiempo de ejecucion: " << duration_search_ns.count() << " nanosegundos.\n";
        }
        else if (opcion == 2) {
            std::cout << "\n>> [CONTEO REAL EN RAM] Total de elementos contados: "
                      << estructura_base.contarElementos() << " nodos activos.\n";
        }
        else if (opcion == 3) {
            int n;
            std::cout << "Cuántos primeros elementos deseas ver?: ";
            std::cin >> n;
            estructura_base.mostrarPrimeros(n);
        }
        else if (opcion == 4) {
            int n;
            std::cout << "Cuántos últimos elementos deseas ver?: ";
            std::cin >> n;
            estructura_base.mostrarUltimos(n);
        }
        else if (opcion == 5) {
            int total_queries;
            int reps;
            std::cout << "Ingresa numero de consultas (ej. 200000): ";
            std::cin >> total_queries;
            std::cout << "Ingresa repeticiones (ej. 7): ";
            std::cin >> reps;
            benchmarkHotset(estructura_base, total_queries, 6145, reps);
        }
    }

    std::cout << "\nCerrando estructura base corregida.\n";
    return 0;
}
