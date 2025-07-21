#include <cmath>

class ModelSolver {
   public:
    // Constructor
    ModelSolver(double Vmax_, double R0_, double a_) : Vmax(Vmax_), R0(R0_), a(a_) {}

    // Compute V(x)
    double compute_V(double x) const {
        if (x <= 0.0) {
            return NAN;  // x must be positive
        }
        return Vmax * x / (R0 + x + a / x);
    }

    // Compute x(V)
    double compute_x(double V) const {
        if (V <= 0.0 || V >= Vmax) {
            return NAN;  // V must be in (0, Vmax)
        }

        double discrim = V * V * R0 * R0 + 4.0 * V * (Vmax - V) * a;
        if (discrim < 0.0) {
            return NAN;  // Should not happen, but just in case
        }

        double numerator = V * R0 + std::sqrt(discrim);
        double denominator = 2.0 * (Vmax - V);

        if (denominator == 0.0) {
            return NAN;  // Should not happen if V < Vmax
        }

        return numerator / denominator;
    }

    // Setters
    void set_R0(double R0_) { R0 = R0_; }
    void set_a(double a_) { a = a_; }
    void set_Vmax(double Vmax_) { Vmax = Vmax_; }

    // Getters
    double get_R0() const { return R0; }
    double get_a() const { return a; }
    double get_Vmax() const { return Vmax; }

   private:
    double Vmax;
    double R0;
    double a;
};
