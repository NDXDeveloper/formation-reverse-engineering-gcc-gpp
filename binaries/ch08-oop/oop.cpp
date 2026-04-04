/**
 * Formation Reverse Engineering — Chapitre 08 : Application C++ orientée objet
 *
 * Ce programme met en scène un petit système de gestion de véhicules
 * utilisant le polymorphisme, l'héritage simple et l'héritage multi-niveaux.
 *
 * Hiérarchie de classes :
 *
 *   Vehicle              (classe racine, concrète)
 *   ├── Car              (héritage simple)
 *   │   └── ElectricCar  (héritage à deux niveaux)
 *   └── Motorcycle       (héritage simple)
 *
 * Concepts C++ illustrés (visibles dans le binaire) :
 *   - vtables et vptr (méthodes virtuelles)
 *   - RTTI (__class_type_info, __si_class_type_info)
 *   - Name mangling Itanium ABI
 *   - Constructeurs chaînés (appel au constructeur parent)
 *   - Destructeurs virtuels (D0 deleting, D1 complete)
 *   - Méthodes non-virtuelles (appel direct, pas de slot vtable)
 *   - std::string en mémoire (SSO, libstdc++)
 *   - Dispatch polymorphe via tableau de pointeurs de base
 *
 * Licence MIT — Usage strictement éducatif.
 */

#include <iostream>
#include <string>
#include <vector>

// ============================================================================
// Classe de base : Vehicle
// ============================================================================

class Vehicle {
public:
    Vehicle(const std::string& name, int fuel_level)
        : name_(name), fuel_level_(fuel_level)
    {
    }

    virtual ~Vehicle()
    {
        std::cout << "Destroying vehicle: " << name_ << std::endl;
    }

    // Méthode virtuelle — surchargée par chaque classe dérivée.
    // Slot [2] dans la vtable (après les deux destructeurs D1/D0).
    virtual void start()
    {
        std::cout << "Starting generic vehicle: " << name_ << std::endl;
        fuel_level_ -= 1;
    }

    // Méthode virtuelle — héritée sans surcharge par toute la hiérarchie.
    // Slot [3] dans la vtable. Le pointeur reste identique dans toutes les vtables.
    virtual void display_info() const
    {
        std::cout << "[Vehicle] name=" << name_
                  << " fuel=" << fuel_level_ << std::endl;
    }

    // Méthode NON-virtuelle — appel direct (CALL immédiat), pas de slot vtable.
    int get_fuel_level() const
    {
        return fuel_level_;
    }

protected:
    std::string name_;       // offset 0x08 (après le vptr), sizeof = 32 (libstdc++ x86-64)
    int         fuel_level_; // offset 0x28
    // padding 4 octets → sizeof(Vehicle) = 0x30 (48)
};

// ============================================================================
// Classe dérivée : Car (hérite de Vehicle)
// ============================================================================

class Car : public Vehicle {
public:
    Car(const std::string& name, int fuel_level, int num_doors)
        : Vehicle(name, fuel_level), num_doors_(num_doors)
    {
    }

    ~Car() override
    {
        std::cout << "Destroying car: " << name_
                  << " (" << num_doors_ << " doors)" << std::endl;
    }

    // Override de Vehicle::start() — le slot [2] de la vtable Car
    // pointe vers Car::start() au lieu de Vehicle::start().
    void start() override
    {
        std::cout << "Starting car: " << name_
                  << " (" << num_doors_ << " doors)" << std::endl;
        fuel_level_ -= 2;
    }

    // display_info() n'est PAS surchargée : le slot [3] de la vtable Car
    // contient le même pointeur que Vehicle::display_info().

    // Méthode NON-virtuelle propre à Car.
    void open_trunk() const
    {
        std::cout << name_ << ": trunk opened." << std::endl;
    }

private:
    int num_doors_; // offset 0x30
    // padding 4 octets → sizeof(Car) = 0x38 (56)
};

// ============================================================================
// Classe dérivée : Motorcycle (hérite de Vehicle)
// ============================================================================

class Motorcycle : public Vehicle {
public:
    Motorcycle(const std::string& name, int fuel_level, bool has_sidecar)
        : Vehicle(name, fuel_level), has_sidecar_(has_sidecar)
    {
    }

    ~Motorcycle() override
    {
        std::cout << "Destroying motorcycle: " << name_ << std::endl;
    }

    // Override de Vehicle::start().
    void start() override
    {
        std::cout << "Kick-starting motorcycle: " << name_;
        if (has_sidecar_) {
            std::cout << " (with sidecar)";
        }
        std::cout << std::endl;
        fuel_level_ -= 1;
    }

    // display_info() héritée sans surcharge, comme pour Car.

private:
    bool has_sidecar_; // offset 0x30, 1 octet + 7 octets padding
    // sizeof(Motorcycle) = 0x38 (56)
};

// ============================================================================
// Classe dérivée de Car : ElectricCar (héritage à deux niveaux)
// ============================================================================

class ElectricCar : public Car {
public:
    ElectricCar(const std::string& name, int fuel_level, int num_doors,
                int battery_kw)
        : Car(name, fuel_level, num_doors), battery_kw_(battery_kw)
    {
    }

    ~ElectricCar() override
    {
        std::cout << "Destroying electric car: " << name_
                  << " (" << battery_kw_ << " kW)" << std::endl;
    }

    // Override de Car::start() (et donc de Vehicle::start()).
    void start() override
    {
        std::cout << "Starting electric vehicle " << name_
                  << " with " << battery_kw_ << " kW battery" << std::endl;
        fuel_level_ -= 1;
    }

    // Méthode NON-virtuelle propre à ElectricCar.
    void charge()
    {
        std::cout << name_ << ": charging battery (" << battery_kw_
                  << " kW)..." << std::endl;
        fuel_level_ = 100;
    }

private:
    int battery_kw_; // offset 0x38
    // padding 4 octets → sizeof(ElectricCar) = 0x40 (64)
};

// ============================================================================
// main — Démonstration du polymorphisme
// ============================================================================

int main()
{
    // Tableau de pointeurs de base — dispatch polymorphe via vtable.
    std::vector<Vehicle*> fleet;

    fleet.push_back(new Vehicle("Truck", 80));
    fleet.push_back(new Car("Sedan", 60, 4));
    fleet.push_back(new Motorcycle("Harley", 20, false));
    fleet.push_back(new ElectricCar("Tesla", 100, 4, 75));

    std::cout << "=== Fleet startup ===" << std::endl;

    // Appels virtuels : start() et display_info() sont résolus via le vptr
    // de chaque objet. Chaque appel passe par la vtable du type réel.
    for (size_t i = 0; i < fleet.size(); ++i) {
        fleet[i]->start();          // dispatch virtuel → slot [2]
        fleet[i]->display_info();   // dispatch virtuel → slot [3] (toujours Vehicle::display_info)
        std::cout << "  fuel_level = " << fleet[i]->get_fuel_level() << std::endl;
        std::cout << std::endl;
    }

    std::cout << "=== Specific actions ===" << std::endl;

    // Appels non-virtuels : nécessitent un cast vers le type dérivé.
    // Dans le binaire, ces appels sont des CALL directs (pas d'indirection vptr).
    static_cast<Car*>(fleet[1])->open_trunk();
    static_cast<ElectricCar*>(fleet[3])->charge();

    std::cout << std::endl;
    std::cout << "=== Cleanup ===" << std::endl;

    // delete via pointeur de base : appelle le destructeur virtuel (D0, deleting)
    // via le slot [1] de la vtable. Le bon destructeur est appelé grâce au
    // polymorphisme, même à travers un Vehicle*.
    for (size_t i = 0; i < fleet.size(); ++i) {
        delete fleet[i];
    }

    return 0;
}
