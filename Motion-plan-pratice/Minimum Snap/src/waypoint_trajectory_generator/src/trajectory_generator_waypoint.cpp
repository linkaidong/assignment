#include "trajectory_generator_waypoint.h"
#include <stdio.h>
#include <ros/ros.h>
#include <ros/console.h>
#include <iostream>
#include <fstream>
#include <string>

using namespace std;    
using namespace Eigen;

TrajectoryGeneratorWaypoint::TrajectoryGeneratorWaypoint(){}
TrajectoryGeneratorWaypoint::~TrajectoryGeneratorWaypoint(){}

//define factorial function, input i, output i!
int TrajectoryGeneratorWaypoint::Factorial(int x)
{
    int fac = 1;
    for(int i = x; i > 0; i--)
        fac = fac * i;
    return fac;
}
/*

    STEP 2: Learn the "Closed-form solution to minimum snap" in L5, then finish this PolyQPGeneration function

    variable declaration: input       const int d_order,                    // the order of derivative
                                      const Eigen::MatrixXd &Path,          // waypoints coordinates (3d)
                                      const Eigen::MatrixXd &Vel,           // boundary velocity
                                      const Eigen::MatrixXd &Acc,           // boundary acceleration
                                      const Eigen::VectorXd &Time)          // time allocation in each segment
                          output      MatrixXd PolyCoeff(m, 3 * p_num1d);   // position(x,y,z), so we need (3 * p_num1d) coefficients

*/

Eigen::MatrixXd TrajectoryGeneratorWaypoint::PolyQPGeneration(
            const int d_order,                    // the order of derivative
            const Eigen::MatrixXd &Path,          // waypoints coordinates (3d)
            const Eigen::MatrixXd &Vel,           // boundary velocity
            const Eigen::MatrixXd &Acc,           // boundary acceleration
            const Eigen::VectorXd &Time)          // time allocation in each segment
{
    // enforce initial and final velocity and accleration, for higher order derivatives, just assume them be 0;
    int p_order = 2 * d_order - 1;              // the order of polynomial
    int p_num1d = p_order + 1;                  // the number of variables in each segment
    int m = Time.size();                        // the number(time_number) of segments

    VectorXd d_F(d_order * 2 + (p_num1d - 2)), d_P((p_num1d - 2) * (d_order - 1));
    MatrixXd Q_m = MatrixXd::Zero(p_num1d, p_num1d);
    MatrixXd A_m = MatrixXd::Zero(2 * d_order,  p_num1d);
    _Q = MatrixXd::Zero(m * Q_m.rows(), m * Q_m.cols());
    MatrixXd A = MatrixXd::Zero(m * A_m.rows(), m * A_m.cols());
    MatrixXd C_T = MatrixXd::Zero(d_order * m * 2, d_order * p_num1d);
    MatrixXd PolyCoeff = MatrixXd::Zero(m, 3 * p_num1d);           // position(x,y,z), so we need (3 * p_num1d) coefficients

    /* Construct the Q matrix */
    for(int i = 0; i < m; i++) {
        for(int j = 0; j <= (p_order - d_order); j++) {
            for(int k = 0; k <= (p_order - d_order); k++) {
                int fac_coeff = (Factorial(p_order - j) * Factorial(p_order - k))
                    / (Factorial(p_order - d_order - j) * Factorial(p_order - d_order - k));
                Q_m(j, k) = pow(Time(i), 2 * (p_order - d_order) + 1 - j - k)
                    * fac_coeff/(2 * (p_order - d_order) + 1 - j - k);
            }
        }
        _Q.block(Q_m.rows() * i, Q_m.rows() * i, Q_m.rows(), Q_m.cols()) = Q_m;
    }

    /*   Produce Mapping Matrix A to the entire trajectory, A is a mapping matrix that maps polynomial coefficients to derivatives.   */
    for(int i = 0; i < m; i++) {
        for(int j = 0; j < d_order; j++) {
            for(int k = 0; k < p_num1d && p_order >= (j + k); k++) {
                int fac_coeff = 0;
                if(p_order >= d_order) {
                    fac_coeff = (Factorial(p_order - k)) / Factorial(p_order - k - j);
                }
                A_m(j, k) = pow(0, p_order - j - k) * fac_coeff;
                A_m(j + d_order, k) = pow(Time(i), p_order - j - k) * fac_coeff;
            }
        }
        A.block(A_m.rows() * i, A_m.cols() * i, A_m.rows(), A_m.cols()) = A_m;
    }

    /* Construct the C and R matrix */
    C_T.block(0, 0, d_order, d_order).setIdentity();   //first of p v a
    C_T.block(C_T.rows() - d_order, d_F.size() - d_order,d_order, d_order).setIdentity();// last of p v a

    for(int i = 0; i <= m - 2; i++) {//d(T, 0 ~ end - 1)
        // right of segment
        C_T(d_order * (2 * i + 1), d_order + i) = 1;
        C_T.block(d_order * (2 * i + 1) + 1, d_F.size() + (d_order - 1) * i, d_order - 1, d_order - 1).setIdentity();
        // left of segment
        C_T(d_order * (2 * i + 2), d_order + i) = 1;
        C_T.block(d_order * (2 * i + 2) + 1, d_F.size() + (d_order - 1) * i , d_order - 1, d_order - 1).setIdentity();
    }
    /*
     * MatrixXd[R_ff R_fp
     *          R_pf R_pp]
     */
    MatrixXd C = C_T.transpose();
    MatrixXd R = C * A.inverse().transpose() * _Q * A.inverse() * C_T;
    MatrixXd R_pp = R.block(d_F.size(), d_F.size(), d_P.size(), d_P.size());
    MatrixXd R_fp = R.block(0, d_F.size(), d_F.size(), d_P.size());

    /*   Produce the dereivatives in X, Y and Z axis directly.  */
    for(int dim = 0; dim < 3; dim++) {
        for(int i = 0; i < p_num1d - 2; i++){
            d_F(d_order + i) = Path(i + 1, dim);
        }

        d_F(0) = Path(0, dim);
        d_F(1) = Vel(0, dim);
        d_F(2) = Acc(0, dim);
        d_F(d_F.size() - d_order) = Path.col(dim).tail(1)(0);
        d_F(d_F.size() - d_order + 1) = Vel.col(dim).tail(1)(0);
        d_F(d_F.size() - d_order + 2) = Acc.col(dim).tail(1)(0);

        d_P = -R_pp.inverse() * R_fp.transpose() * d_F;
        VectorXd d(d_F.size() + d_P.size());
        d.head(d_F.size()) = d_F;
        d.tail(d_P.size()) = d_P;
        VectorXd P = A.inverse() * C_T * d;

        for(int i = 0; i < m; i++) {
            if(dim == 0) {
                _Px = P.segment(i * p_num1d, p_num1d);
                PolyCoeff.row(i).segment(0, p_num1d) = _Px.transpose();
            }
            if(dim == 1) {
                _Py = P.segment(i * p_num1d, p_num1d);
                PolyCoeff.row(i).segment(1 * p_num1d, p_num1d) = _Py.transpose();
            }
            if(dim == 2) {
                _Pz = P.segment(i * p_num1d, p_num1d);
                PolyCoeff.row(i).segment(2 * p_num1d, p_num1d) = _Pz.transpose();
            }
        }
    }
    return PolyCoeff;
}
