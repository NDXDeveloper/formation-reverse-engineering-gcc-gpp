/**
 * Formation Reverse Engineering — Chapitre 17 
 * Binaire d'entraînement C++ orienté objet
 *
 * Ce programme exerce volontairement un maximum de mécanismes C++ que
 * le reverse engineer doit savoir reconnaître dans un binaire GCC :
 *
 *   - Hiérarchie de classes (héritage simple + multiple)
 *   - Méthodes virtuelles et virtuelles pures (classes abstraites)
 *   - RTTI activée (dynamic_cast, typeid)
 *   - Exceptions personnalisées (try/catch, __cxa_throw)
 *   - Conteneurs STL (vector, string, map, unordered_map)
 *   - Templates instanciés avec plusieurs types
 *   - Lambdas avec différents modes de capture
 *   - Smart pointers (unique_ptr, shared_ptr)
 *
 * Compilation : voir Makefile (make all)
 * Licence : MIT — Usage strictement éducatif
 */

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <cmath>
#include <cstring>
#include <typeinfo>

// ============================================================================
// 1. EXCEPTIONS PERSONNALISÉES
//    En RE : repérer __cxa_allocate_exception, __cxa_throw, __cxa_begin_catch
//            et les typeinfo dans .rodata
// ============================================================================

class AppException : public std::exception {
protected:
    std::string msg_;
    int code_;
public:
    AppException(const std::string& msg, int code)
        : msg_(msg), code_(code) {}

    const char* what() const noexcept override {
        return msg_.c_str();
    }

    int code() const noexcept { return code_; }
};

class ParseError : public AppException {
    int line_;
public:
    ParseError(const std::string& msg, int line)
        : AppException(msg, 100), line_(line) {}

    int line() const noexcept { return line_; }
};

class NetworkError : public AppException {
    std::string host_;
public:
    NetworkError(const std::string& msg, const std::string& host)
        : AppException(msg, 200), host_(host) {}

    const std::string& host() const noexcept { return host_; }
};

// ============================================================================
// 2. HIÉRARCHIE DE CLASSES — HÉRITAGE SIMPLE
//    En RE : vtable par classe dans .rodata, vptr à l'offset 0 de chaque objet,
//            constructeurs qui initialisent le vptr
// ============================================================================

class Shape {
protected:
    std::string name_;
    double x_, y_;  // position

public:
    Shape(const std::string& name, double x, double y)
        : name_(name), x_(x), y_(y) {}

    virtual ~Shape() = default;

    // Méthode virtuelle pure → Shape est abstraite
    virtual double area() const = 0;
    virtual double perimeter() const = 0;

    // Méthode virtuelle avec implémentation par défaut
    virtual std::string describe() const {
        return name_ + " at (" +
               std::to_string(x_) + ", " +
               std::to_string(y_) + ")";
    }

    // Méthode non-virtuelle
    const std::string& name() const { return name_; }

    void move(double dx, double dy) {
        x_ += dx;
        y_ += dy;
    }
};

class Circle : public Shape {
    double radius_;
public:
    Circle(double x, double y, double r)
        : Shape("Circle", x, y), radius_(r) {
        if (r <= 0) throw AppException("Invalid radius", 10);
    }

    double area() const override {
        return M_PI * radius_ * radius_;
    }

    double perimeter() const override {
        return 2.0 * M_PI * radius_;
    }

    std::string describe() const override {
        return Shape::describe() + " r=" + std::to_string(radius_);
    }

    double radius() const { return radius_; }
};

class Rectangle : public Shape {
    double width_, height_;
public:
    Rectangle(double x, double y, double w, double h)
        : Shape("Rectangle", x, y), width_(w), height_(h) {
        if (w <= 0 || h <= 0) throw AppException("Invalid dimensions", 11);
    }

    double area() const override {
        return width_ * height_;
    }

    double perimeter() const override {
        return 2.0 * (width_ + height_);
    }

    std::string describe() const override {
        return Shape::describe() +
               " w=" + std::to_string(width_) +
               " h=" + std::to_string(height_);
    }
};

class Triangle : public Shape {
    double a_, b_, c_;  // longueurs des côtés
public:
    Triangle(double x, double y, double a, double b, double c)
        : Shape("Triangle", x, y), a_(a), b_(b), c_(c) {
        if (a <= 0 || b <= 0 || c <= 0)
            throw AppException("Invalid side length", 12);
        if (a + b <= c || a + c <= b || b + c <= a)
            throw AppException("Triangle inequality violated", 13);
    }

    double area() const override {
        double s = (a_ + b_ + c_) / 2.0;
        return std::sqrt(s * (s - a_) * (s - b_) * (s - c_));
    }

    double perimeter() const override {
        return a_ + b_ + c_;
    }
};

// ============================================================================
// 3. HÉRITAGE MULTIPLE
//    En RE : plusieurs vptr dans un même objet, thunks d'ajustement,
//            vtables multiples dans .rodata pour une même classe
// ============================================================================

class Drawable {
public:
    virtual ~Drawable() = default;
    virtual void draw() const = 0;
    virtual int zOrder() const { return 0; }
};

class Serializable {
public:
    virtual ~Serializable() = default;
    virtual std::string serialize() const = 0;
    virtual bool deserialize(const std::string& data) = 0;
};

// Héritage multiple : Canvas hérite de Drawable ET Serializable
class Canvas : public Drawable, public Serializable {
    std::string title_;
    std::vector<std::shared_ptr<Shape>> shapes_;
    int z_order_;

public:
    Canvas(const std::string& title, int z = 0)
        : title_(title), z_order_(z) {}

    void addShape(std::shared_ptr<Shape> shape) {
        shapes_.push_back(shape);
    }

    // Implémente Drawable::draw
    void draw() const override {
        std::cout << "=== Canvas: " << title_
                  << " (" << shapes_.size() << " shapes) ===" << std::endl;
        for (const auto& s : shapes_) {
            std::cout << "  " << s->describe()
                      << " | area=" << s->area() << std::endl;
        }
    }

    int zOrder() const override { return z_order_; }

    // Implémente Serializable::serialize
    std::string serialize() const override {
        std::string out = "CANVAS:" + title_ + ":";
        out += std::to_string(shapes_.size());
        for (const auto& s : shapes_) {
            out += "|" + s->name() + ":" + std::to_string(s->area());
        }
        return out;
    }

    bool deserialize(const std::string& data) override {
        // Implémentation simplifiée pour l'exercice RE
        if (data.substr(0, 7) != "CANVAS:")
            throw ParseError("Invalid canvas header", 1);
        title_ = data.substr(7, data.find(':', 7) - 7);
        return true;
    }

    double totalArea() const {
        double total = 0;
        for (const auto& s : shapes_) {
            total += s->area();
        }
        return total;
    }

    const std::string& title() const { return title_; }
    size_t shapeCount() const { return shapes_.size(); }
};

// ============================================================================
// 4. TEMPLATES
//    En RE : instanciations multiples → symboles dupliqués avec types différents,
//            reconnaître les patterns identiques avec des tailles d'accès variées
// ============================================================================

template<typename K, typename V>
class Registry {
    std::map<K, V> entries_;
    std::string name_;

public:
    explicit Registry(const std::string& name) : name_(name) {}

    void add(const K& key, const V& value) {
        if (entries_.count(key))
            throw AppException("Duplicate key in registry: " + name_, 300);
        entries_[key] = value;
    }

    const V& get(const K& key) const {
        auto it = entries_.find(key);
        if (it == entries_.end())
            throw AppException("Key not found in registry: " + name_, 301);
        return it->second;
    }

    bool contains(const K& key) const {
        return entries_.count(key) > 0;
    }

    size_t size() const { return entries_.size(); }

    // Itération avec callback (exercice pour les lambdas)
    void forEach(std::function<void(const K&, const V&)> callback) const {
        for (const auto& [k, v] : entries_) {
            callback(k, v);
        }
    }

    // Filtrage avec prédicat template
    template<typename Pred>
    std::vector<K> filter(Pred predicate) const {
        std::vector<K> result;
        for (const auto& [k, v] : entries_) {
            if (predicate(k, v)) {
                result.push_back(k);
            }
        }
        return result;
    }
};

// ============================================================================
// 5. FONCTIONS UTILISANT DES LAMBDAS
//    En RE : classes anonymes générées par le compilateur, operator() dans la
//            vtable, captures visibles dans le layout mémoire de la closure
// ============================================================================

static void demonstrateLambdas(const std::vector<std::shared_ptr<Shape>>& shapes) {
    std::cout << "\n--- Lambda demonstrations ---" << std::endl;

    // Lambda sans capture
    auto printSeparator = []() {
        std::cout << "------------------------" << std::endl;
    };

    // Lambda avec capture par valeur
    double minArea = 10.0;
    auto isLargeShape = [minArea](const std::shared_ptr<Shape>& s) {
        return s->area() > minArea;
    };

    // Lambda avec capture par référence
    double totalArea = 0.0;
    int count = 0;
    auto accumulate = [&totalArea, &count](const std::shared_ptr<Shape>& s) {
        totalArea += s->area();
        count++;
    };

    // Lambda avec capture mixte (valeur + référence)
    std::string prefix = ">> ";
    std::vector<std::string> descriptions;
    auto describeAndCollect = [prefix, &descriptions](const std::shared_ptr<Shape>& s) {
        std::string desc = prefix + s->describe();
        descriptions.push_back(desc);
        std::cout << desc << std::endl;
    };

    // Lambda générique (C++14) — capture par copie de tout
    auto formatShape = [=](const auto& s) -> std::string {
        return prefix + s->name() + " (area >= " +
               std::to_string(minArea) + "? " +
               (s->area() >= minArea ? "yes" : "no") + ")";
    };

    printSeparator();

    // Utilisation avec std::for_each
    std::for_each(shapes.begin(), shapes.end(), accumulate);
    std::cout << "Total shapes: " << count
              << ", Total area: " << totalArea << std::endl;

    printSeparator();

    // Utilisation avec std::count_if
    auto largeCount = std::count_if(shapes.begin(), shapes.end(), isLargeShape);
    std::cout << "Large shapes (area > " << minArea << "): "
              << largeCount << std::endl;

    printSeparator();

    // Décrire chaque shape
    std::for_each(shapes.begin(), shapes.end(), describeAndCollect);

    printSeparator();

    // Utiliser la lambda générique
    for (const auto& s : shapes) {
        std::cout << formatShape(s) << std::endl;
    }
}

// ============================================================================
// 6. FONCTIONS UTILISANT DES SMART POINTERS
//    En RE : unique_ptr → quasi transparent (inliné), shared_ptr → compteur
//            atomique, control block, appels à __shared_count
// ============================================================================

struct Config {
    std::string name;
    int maxShapes;
    bool verbose;

    Config(const std::string& n, int m, bool v)
        : name(n), maxShapes(m), verbose(v) {}
};

static void demonstrateSmartPointers() {
    std::cout << "\n--- Smart pointer demonstrations ---" << std::endl;

    // unique_ptr — transfert de propriété
    auto config = std::make_unique<Config>("default", 100, true);
    std::cout << "Config: " << config->name
              << ", max=" << config->maxShapes << std::endl;

    // Transfert de ownership
    auto config2 = std::move(config);
    // config est maintenant nullptr
    if (!config) {
        std::cout << "Original config moved, now null" << std::endl;
    }
    std::cout << "Moved config: " << config2->name << std::endl;

    // shared_ptr — propriété partagée avec comptage de références
    auto sharedCircle = std::make_shared<Circle>(0, 0, 5.0);
    std::cout << "Circle refcount: " << sharedCircle.use_count() << std::endl;

    {
        // Copie → incrémente le compteur
        auto copy1 = sharedCircle;
        auto copy2 = sharedCircle;
        std::cout << "After 2 copies, refcount: "
                  << sharedCircle.use_count() << std::endl;

        // weak_ptr — référence faible, ne maintient pas l'objet en vie
        std::weak_ptr<Circle> weakRef = sharedCircle;
        std::cout << "Weak ref expired? "
                  << (weakRef.expired() ? "yes" : "no") << std::endl;

        if (auto locked = weakRef.lock()) {
            std::cout << "Locked weak_ptr, radius: "
                      << locked->radius() << std::endl;
            std::cout << "Refcount during lock: "
                      << sharedCircle.use_count() << std::endl;
        }
    }
    // copy1 et copy2 détruits → compteur redescend
    std::cout << "After scope exit, refcount: "
              << sharedCircle.use_count() << std::endl;

    // shared_ptr dans un conteneur
    std::vector<std::shared_ptr<Shape>> shapeVec;
    shapeVec.push_back(sharedCircle);
    shapeVec.push_back(std::make_shared<Rectangle>(1, 1, 4, 3));
    shapeVec.push_back(std::make_shared<Triangle>(0, 0, 3, 4, 5));

    std::cout << "Circle refcount in vector: "
              << sharedCircle.use_count() << std::endl;

    // unique_ptr avec tableau (rare mais existe en RE)
    auto buffer = std::make_unique<char[]>(256);
    std::strcpy(buffer.get(), "RE training buffer");
    std::cout << "Buffer content: " << buffer.get() << std::endl;
}

// ============================================================================
// 7. UTILISATION DE std::unordered_map ET RTTI
//    En RE : hash table internals, typeid / dynamic_cast
// ============================================================================

static void demonstrateRTTI(const std::vector<std::shared_ptr<Shape>>& shapes) {
    std::cout << "\n--- RTTI demonstrations ---" << std::endl;

    // Comptage par type avec typeid
    std::unordered_map<std::string, int> typeCounts;

    for (const auto& s : shapes) {
        // typeid produit des références à typeinfo dans .rodata
        std::string typeName = typeid(*s).name();
        typeCounts[typeName]++;

        std::cout << "Shape: " << s->name()
                  << " | typeid: " << typeName << std::endl;
    }

    std::cout << "\nType distribution:" << std::endl;
    for (const auto& [type, count] : typeCounts) {
        std::cout << "  " << type << ": " << count << std::endl;
    }

    // dynamic_cast — vérification de type à l'exécution
    for (const auto& s : shapes) {
        if (auto* circle = dynamic_cast<Circle*>(s.get())) {
            std::cout << "Found Circle with radius: "
                      << circle->radius() << std::endl;
        } else if (dynamic_cast<Rectangle*>(s.get())) {
            std::cout << "Found Rectangle, area: "
                      << s->area() << std::endl;
        } else if (dynamic_cast<Triangle*>(s.get())) {
            std::cout << "Found Triangle, perimeter: "
                      << s->perimeter() << std::endl;
        }
    }
}

// ============================================================================
// 8. POINT D'ENTRÉE — EXERCICE TOUT LE PROGRAMME
// ============================================================================

int main(int argc, char* argv[]) {
    bool verbose = false;
    if (argc > 1 && std::string(argv[1]) == "-v") {
        verbose = true;
    }

    try {
        // --- Création des shapes (smart pointers) ---
        auto c1 = std::make_shared<Circle>(0, 0, 5.0);
        auto c2 = std::make_shared<Circle>(10, 10, 3.0);
        auto r1 = std::make_shared<Rectangle>(5, 5, 4.0, 6.0);
        auto r2 = std::make_shared<Rectangle>(0, 10, 2.0, 8.0);
        auto t1 = std::make_shared<Triangle>(3, 3, 3.0, 4.0, 5.0);

        std::vector<std::shared_ptr<Shape>> allShapes = {c1, c2, r1, r2, t1};

        // --- Registry (template instancié avec deux types) ---
        Registry<std::string, std::shared_ptr<Shape>> shapeRegistry("shapes");
        shapeRegistry.add("main_circle", c1);
        shapeRegistry.add("small_circle", c2);
        shapeRegistry.add("main_rect", r1);
        shapeRegistry.add("side_rect", r2);
        shapeRegistry.add("triangle", t1);

        Registry<int, std::string> idRegistry("ids");
        idRegistry.add(1, "circle_main");
        idRegistry.add(2, "circle_small");
        idRegistry.add(3, "rect_main");
        idRegistry.add(4, "rect_side");
        idRegistry.add(5, "triangle");

        // --- Accès via le registry ---
        std::cout << "Registry lookup: "
                  << shapeRegistry.get("main_circle")->describe() << std::endl;
        std::cout << "ID lookup: "
                  << idRegistry.get(3) << std::endl;

        // --- Filtrage avec lambda dans le template ---
        auto largeShapeKeys = shapeRegistry.filter(
            [](const std::string& key, const std::shared_ptr<Shape>& s) {
                return s->area() > 20.0;
            }
        );
        std::cout << "\nShapes with area > 20:" << std::endl;
        for (const auto& key : largeShapeKeys) {
            std::cout << "  " << key << ": "
                      << shapeRegistry.get(key)->describe() << std::endl;
        }

        // --- Canvas (héritage multiple) ---
        Canvas canvas("Main Canvas", 1);
        for (const auto& s : allShapes) {
            canvas.addShape(s);
        }
        canvas.draw();

        std::cout << "\nCanvas total area: "
                  << canvas.totalArea() << std::endl;

        // --- Serialization (Serializable interface) ---
        std::string serialized = canvas.serialize();
        std::cout << "Serialized: " << serialized << std::endl;

        // --- Drawable interface ---
        Drawable* drawable = &canvas;
        drawable->draw();
        std::cout << "Z-order: " << drawable->zOrder() << std::endl;

        // --- Deserialization test ---
        Canvas canvas2("Empty", 0);
        canvas2.deserialize(serialized);
        std::cout << "Deserialized canvas title: "
                  << canvas2.title() << std::endl;

        // --- Lambdas ---
        demonstrateLambdas(allShapes);

        // --- Smart pointers ---
        demonstrateSmartPointers();

        // --- RTTI ---
        demonstrateRTTI(allShapes);

        // --- forEach avec le Registry ---
        if (verbose) {
            std::cout << "\n--- Full registry dump ---" << std::endl;
            shapeRegistry.forEach(
                [](const std::string& key, const std::shared_ptr<Shape>& s) {
                    std::cout << "  [" << key << "] "
                              << s->describe()
                              << " | area=" << s->area()
                              << " | perimeter=" << s->perimeter()
                              << std::endl;
                }
            );

            idRegistry.forEach(
                [](const int& id, const std::string& name) {
                    std::cout << "  ID " << id << " -> " << name << std::endl;
                }
            );
        }

        // --- Polymorphisme via pointeur de base ---
        std::cout << "\n--- Polymorphic iteration ---" << std::endl;
        for (const auto& s : allShapes) {
            // Appel virtuel : le vptr détermine quelle méthode est appelée
            std::cout << s->describe()
                      << " | area=" << s->area()
                      << " | perimeter=" << s->perimeter()
                      << std::endl;
        }

    } catch (const ParseError& e) {
        std::cerr << "[ParseError] line " << e.line()
                  << ": " << e.what()
                  << " (code " << e.code() << ")" << std::endl;
        return 2;
    } catch (const NetworkError& e) {
        std::cerr << "[NetworkError] host " << e.host()
                  << ": " << e.what()
                  << " (code " << e.code() << ")" << std::endl;
        return 3;
    } catch (const AppException& e) {
        std::cerr << "[AppException] " << e.what()
                  << " (code " << e.code() << ")" << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[std::exception] " << e.what() << std::endl;
        return 99;
    }

    std::cout << "\n✓ All demonstrations completed." << std::endl;
    return 0;
}
