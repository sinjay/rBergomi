/*
 * rBergomiMT.cpp
 *
 * For the multi-threaded version using OMP, we would need to copy
 * essentially all the data members in the rBergomi class. This does not make sense,
 * hence we do not use a class for this, but functions. We copy the functionality
 * already implemented in the single-threaded version.
 *
 *  Created on: Feb 14, 2017
 *      Author: bayerc
 */

#include "rBergomiMT.h"

fftData::~fftData() {
	if (numThreads > 0) {
		for (int i = 0; i < numThreads; ++i) {
			delete[] xC[i];
			delete[] xHat[i];
			delete[] yC[i];
			delete[] yHat[i];
			delete[] zC[i];
			delete[] zHat[i];
		}
	}
	delete[] xC;
	delete[] xHat;
	delete[] yC;
	delete[] yHat;
	delete[] zC;
	delete[] zHat;
}

fftData::fftData(int nDFT, int numThreads) {
	this->numThreads = numThreads;
	xC = new fftw_complex*[numThreads];
	xHat = new fftw_complex*[numThreads];
	yC = new fftw_complex*[numThreads];
	yHat = new fftw_complex*[numThreads];
	zC = new fftw_complex*[numThreads];
	zHat = new fftw_complex*[numThreads];
	for (int i = 0; i < numThreads; ++i) {
		xC[i] = new fftw_complex[nDFT];
		xHat[i] = new fftw_complex[nDFT];
		yC[i] = new fftw_complex[nDFT];
		yHat[i] = new fftw_complex[nDFT];
		zC[i] = new fftw_complex[nDFT];
		zHat[i] = new fftw_complex[nDFT];
	}
	fPlanX = std::vector<fftw_plan>(numThreads);
	fPlanY = std::vector<fftw_plan>(numThreads);
	fPlanZ = std::vector<fftw_plan>(numThreads);
	for (int i = 0; i < numThreads; ++i) {
		fPlanX[i] = fftw_plan_dft_1d(nDFT, xC[i], xHat[i], FFTW_FORWARD,
				FFTW_PATIENT);
				//FFTW_ESTIMATE);
		fPlanY[i] = fftw_plan_dft_1d(nDFT, yC[i], yHat[i], FFTW_FORWARD,
				FFTW_PATIENT);
				//FFTW_ESTIMATE);
		fPlanZ[i] = fftw_plan_dft_1d(nDFT, zHat[i], zC[i], FFTW_BACKWARD,
				FFTW_PATIENT);
				//FFTW_ESTIMATE);
	}
}

RNG::RNG(int numThreads, std::vector<uint64_t> seed) {
	std::seed_seq seeder(seed.begin(), seed.end());
	std::vector<uint64_t> seed_vec(numThreads);
	seeder.generate(seed_vec.begin(), seed_vec.end());
	gen = std::vector<MTGenerator>(numThreads);
	for (int i = 0; i < numThreads; ++i) {
		gen[i].seed(seed_vec[i]);
	}
}

void updateV(Vector & v, const Vector & WtildeScaled, double xi, double H,
		double eta, double dt) {
	v[0] = xi;
	for (int i = 1; i < v.size(); ++i)
		v[i] = xi * exp(
				eta * WtildeScaled[i - 1] - 0.5 * eta * eta * pow((i - 1) * dt,
						2 * H));
}

// Here we need to be careful due to the parallelization...
void updateWtilde(Vector & Wtilde, const Vector & W1, const Vector & W1perp,
		double H, std::map<double, Vector> GammaMap, fftData& fft,
		int nDFT) {
	int N = Wtilde.size();
	double s2H = sqrt(2.0 * H);
	double rhoH = s2H / (H + 0.5);
	Vector W1hat = linearComb(rhoH / s2H, W1, sqrt(1.0 - rhoH * rhoH) / s2H, W1perp);
	Vector Y2(N); // see R code
	// Convolve W1 and Gamma
	// Copy W1 and Gamma to complex arrays
	copyToComplex(nDFT, W1, fft.xC[omp_get_thread_num()]);
	copyToComplex(nDFT, GammaMap[H], fft.yC[omp_get_thread_num()]);
	// DFT both
	fftw_execute(fft.fPlanX[omp_get_thread_num()]); // DFT saved in xHat[0]
	fftw_execute(fft.fPlanY[omp_get_thread_num()]); // DFT saved in yHat[0]
	// multiply xHat and yHat and save in zHat
	complexMult(nDFT, fft.xHat[omp_get_thread_num()], fft.yHat[omp_get_thread_num()],
			fft.zHat[omp_get_thread_num()]);
	// inverse DFT zHat
	fftw_execute(fft.fPlanZ[omp_get_thread_num()]);
	// read out the real part, re-scale by 1/nDFT
	copyToReal(Y2, fft.zC[omp_get_thread_num()]);
	scaleVector(Y2, 1.0 / nDFT);
	// Wtilde = (Y2 + W1hat) * sqrt(2*H) * dt^H ??
	Wtilde = linearComb(sqrt(2.0 * H) * pow(1.0 / N, H), Y2,
			sqrt(2.0 * H) * pow(1.0 / N, H), W1hat);
}

// Using new Convolve class
void updateWtildeConv(Vector & Wtilde, const Vector & W1, const Vector & W1perp,
		double H, std::map<double, Vector> GammaMap, Convolve* conv){
	int N = Wtilde.size();
	double s2H = sqrt(2.0 * H);
	double rhoH = s2H / (H + 0.5);
	Vector W1hat = linearComb(rhoH / s2H, W1, sqrt(1.0 - rhoH * rhoH) / s2H, W1perp);
	Vector Y2(N); // see R code
	// Convolve W1 and Gamma and save to Y2
	conv->run(W1, GammaMap[H], Y2);
	// Wtilde = (Y2 + W1hat) * sqrt(2*H) * dt^H ??
	Wtilde = linearComb(sqrt(2.0 * H) * pow(1.0 / N, H), Y2,
			sqrt(2.0 * H) * pow(1.0 / N, H), W1hat);
}

double updateS(const Vector & v, const Vector & ZScaled, double dt) {
	double X = 0.0;
	for (size_t i = 0; i < v.size(); ++i)
		X += sqrt(v[i]) * ZScaled[i] - 0.5 * v[i] * dt;
	return exp(X); // Recall S_0 = 1.
}

void complexMult(int nDFT, const fftw_complex *x, const fftw_complex *y,
		fftw_complex *z) {
	for (int i = 0; i < nDFT; ++i)
		fftw_c_mult(x[i], y[i], z[i]);
}

void copyToReal(Vector & x, const fftw_complex *xc) {
	for (size_t i = 0; i < x.size(); ++i)
		x[i] = xc[i][0]; // real part
}

void scaleZ(Vector & ZScaled, const Vector & Z, double sdt) {
	for (int i = 0; i < ZScaled.size(); ++i)
		ZScaled[i] = sdt * Z[i];
}

void updateZ(Vector & Z, const Vector & W1, const Vector & Wperp, double rho) {
	Z = linearComb(rho, W1, sqrt(1.0 - rho * rho), Wperp);
}

void fftw_c_mult(const fftw_complex a, const fftw_complex b, fftw_complex c) {
	c[0] = a[0] * b[0] - a[1] * b[1];
	c[1] = a[0] * b[1] + a[1] * b[0];
}

void getGamma(Vector & Gamma, double H) {
	double alpha = H - 0.5;
	Gamma[0] = 0.0;
	for (int i = 1; i < Gamma.size(); ++i)
		Gamma[i] = (pow(i + 1.0, alpha + 1.0) - pow(i, alpha + 1.0)) / (alpha
				+ 1.0);
}

void copyToComplex(int nDFT, const Vector & x, fftw_complex *xc) {
	for (size_t i = 0; i < x.size(); ++i) {
		xc[i][0] = x[i]; // real part
		xc[i][1] = 0.0; // imaginary part
	}
	// fill up with 0es
	for (size_t i = x.size(); i < nDFT; ++i) {
		xc[i][0] = 0.0; // real part
		xc[i][1] = 0.0; // imaginary part
	}
}

void scaleWtilde(Vector & WtildeScaled, const Vector & Wtilde, double T,
		double H) {
	for (int i = 0; i < WtildeScaled.size(); ++i)
		WtildeScaled[i] = pow(T, H) * Wtilde[i];
}

void genGaussianMT(Vector & X, RNG & rng, int threadID) {
	for (int i = 0; i < X.size(); ++i)
		X[i] = rng.dist(rng.gen[threadID]); // replace 0 by omp_get_thread_num() for multi-threaded code
}

Result ComputePriceMT(double xi, Vector H, Vector eta, Vector rho, Vector T,
		Vector K, int N, long M, int numThreads, std::vector<uint64_t> seed) {
	// The random vectors; the first 3 are independent, Z is a composite
	// Note that W1, W1perp, Wperp, Z correspond to UNNORMALIZED increments of Brownian motions,
	// i.e., are i.i.d. standard normal.
	Vector W1(N);
	Vector W1perp(N);
	Vector Wperp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector ZScaled(N);
	Vector v(N);
	Vector Z(N);

	double S; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// vectors of prices and variances
	Vector price(par.size(), 0.0);
	Vector var(par.size(), 0.0);

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// gather the RNG
	RNG rng(numThreads, seed);

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(numThreads);

	// The big loop which needs to be parallelized in future
	// rng is firstprivate because of the shared dist
	// Also try what happens if rng is shared... Has negligible effect on MAC, but does not make it work in WIAS
	#pragma omp parallel \
		firstprivate(W1, W1perp, Wperp, Wtilde, WtildeScaled, Z, ZScaled, v, rng, GammaMap), \
		private(S, dt, sdt)
	{
		Vector price_private(par.size(), 0.0);
		Vector var_private(par.size(), 0.0);
	#pragma omp for
		for (int m = 0; m < M; ++m) {
			// generate the fundamental Gaussians
			genGaussianMT(W1, rng, omp_get_thread_num());
			genGaussianMT(W1perp, rng, omp_get_thread_num());
			genGaussianMT(Wperp, rng, omp_get_thread_num());

			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {
				// Note that each of the changes here forces all subsequent updates!
				// check if H has changed. If so, Wtilde needs to be updated (and, hence, everything else)
				bool update = par.HTrigger(i);
				if (update)
					updateWtilde(Wtilde, W1, W1perp, par.H(i), GammaMap, fft, nDFT);
				// check if T has changed. If so, Wtilde and the time increment need re-scaling
				update = update || par.TTrigger(i);
				if (update) {
					scaleWtilde(WtildeScaled, Wtilde, par.T(i), par.H(i));
					dt = par.T(i) / N;
					sdt = sqrt(dt);
				}
				// check if eta has changed. If so, v needs to be updated
				update = update || par.etaTrigger(i);
				if (update)
					updateV(v, WtildeScaled, xi, par.H(i), par.eta(i), dt);
				// if rho has changed, then S needs to be re-computed
				update = update || par.rhoTrigger(i);
				if (update) {
					updateZ(Z, W1, Wperp, par.rho(i));
					scaleZ(ZScaled, Z, sdt);
					S = updateS(v, ZScaled, dt);
				}
				// now compute the payoff
				double payoff = posPart(S - par.K(i)); // call option
				price_private[i] += payoff;
				var_private[i] += payoff * payoff;
			}
		}
		// now sum all the prices and variances from the individual threads
		#pragma omp critical
		{
			for (long i = 0; i < par.size(); ++i) {
				price[i] += price_private[i];
				var[i] += var_private[i];
			}
		}
	}

	// compute mean and variance
	scaleVector(price, 1.0 / double(M));
	scaleVector(var, 1.0 / double(M)); // = E[X^2]
	var = linearComb(1.0, var, -1.0, squareVector(price)); // = empirical var of price
	Vector stat = rootVector(var);
	scaleVector(stat, 1.0 / sqrt(double(M)));
	Vector iv(par.size());
	Result res { price, iv, par, stat, N, M, numThreads, 0.0 };

	return res;
}

Result ComputeIVMT(double xi, Vector H, Vector eta, Vector rho, Vector T,
		Vector K, int N, long M, int numThreads, std::vector<uint64_t> seed) {
	Result res = ComputePriceMT(xi, H, eta, rho, T, K, N, M, numThreads, seed);
	// Compute the implied vol for each price saved in res
	for (long i = 0; i < res.par.size(); ++i) {
		res.iv[i] = IV_call(res.price[i], 1.0, res.par.K(i), res.par.T(i));
	}
	return res;
}

Result ComputePriceRTMT(double xi, Vector H, Vector eta, Vector rho, Vector T,
		Vector K, int N, long M, int numThreads, std::vector<uint64_t> seed) {
	// The random vectors; the first 3 are independent, Z is a composite
	// Note that W1, W1perp, Wperp, Z correspond to UNNORMALIZED increments of Brownian motions,
	// i.e., are i.i.d. standard normal.
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// vectors of prices and variances
	Vector price(par.size(), 0.0);
	Vector var(par.size(), 0.0);

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// gather the RNG
	RNG rng(numThreads, seed);

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(numThreads);

	// The big loop which needs to be parallelized in future
	// rng is firstprivate because of the shared dist
	// Also try what happens if rng is shared... Has negligible effect on MAC, but does not make it work in WIAS
	#pragma omp parallel \
		firstprivate(W1, Wtilde, WtildeScaled, v, rng, GammaMap), \
		private(Ivdt, IsvdW, dt, sdt)
	{
		Vector price_private(par.size(), 0.0);
		Vector var_private(par.size(), 0.0);
		#pragma omp for
		for (int m = 0; m < M; ++m) {
			// generate the fundamental Gaussians
			genGaussianMT(W1, rng, omp_get_thread_num());
			genGaussianMT(W1perp, rng, omp_get_thread_num());

			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {

				// Note that each of the changes here forces all subsequent updates!
				// check if H has changed. If so, Wtilde needs to be updated (and, hence, everything else)
				bool update = par.HTrigger(i);
				if (update)
					updateWtilde(Wtilde, W1, W1perp, par.H(i), GammaMap, fft, nDFT);
				// check if T has changed. If so, Wtilde and the time increment need re-scaling
				update = update || par.TTrigger(i);
				if (update) {
					scaleWtilde(WtildeScaled, Wtilde, par.T(i), par.H(i));
					dt = par.T(i) / N;
					sdt = sqrt(dt);
				}
				// check if eta has changed. If so, v needs to be updated
				update = update || par.etaTrigger(i);
				if (update)
					updateV(v, WtildeScaled, xi, par.H(i), par.eta(i), dt);
				// now compute \int v_s ds, \int \sqrt{v_s} dW_s with W = W1
				Ivdt = intVdt(v, dt);
				IsvdW = intRootVdW(v, W1, sdt);

				// now compute the payoff by inserting properly into the BS formula
				double BS_vol = sqrt((1.0 - par.rho(i) * par.rho(i)) * Ivdt);
				double BS_spot = exp(
						-0.5 * par.rho(i) * par.rho(i) * Ivdt + par.rho(i)
								* IsvdW);
				double payoff = BS_call_price(BS_spot, par.K(i), 1.0, BS_vol);

				//double payoff =  updatePayoff(par, xi, i, Wtilde, WtildeScaled, W1, W1perp, v,
				//						GammaMap, fft, nDFT, N, omp_get_thread_num());

				price_private[i] += payoff;
				var_private[i] += payoff * payoff;
			}
		}
		// now sum all the prices and variances from the individual threads
		#pragma omp critical
		{
			for (long i = 0; i < par.size(); ++i) {
				price[i] += price_private[i];
				var[i] += var_private[i];
			}
		}
	}

	// compute mean and variance
	scaleVector(price, 1.0 / double(M));
	scaleVector(var, 1.0 / double(M)); // = E[X^2]
	var = linearComb(1.0, var, -1.0, squareVector(price)); // = empirical var of price
	Vector stat = rootVector(var);
	scaleVector(stat, 1.0 / sqrt(double(M)));
	Vector iv(par.size());
	Result res { price, iv, par, stat, N, M, numThreads, 0.0 };

	return res;
}

Result ComputeIVRTMT(double xi, Vector H, Vector eta, Vector rho, Vector T,
		Vector K, int N, long M, int numThreads, std::vector<uint64_t> seed) {
	Result res =
			ComputePriceRTMT(xi, H, eta, rho, T, K, N, M, numThreads, seed);
	// Compute the implied vol for each price saved in res
	for (long i = 0; i < res.par.size(); ++i) {
		res.iv[i] = IV_call(res.price[i], 1.0, res.par.K(i), res.par.T(i));
	}
	return res;
}

double intVdt(const Vector & v, double dt) {
	return dt * std::accumulate(v.begin(), v.end(), 0.0);
}

double intRootVdW(const Vector & v, const Vector & W1, double sdt) {
	double IsvdW = 0.0;
	for (size_t i = 0; i < v.size(); ++i)
		IsvdW += sqrt(v[i]) * sdt * W1[i];
	return IsvdW;
}

std::vector<Vector> ComputePayoffRTsamples(double xi, Vector H, Vector eta,
		Vector rho, Vector T, Vector K, int numThreads,
		const std::vector<Vector> & W1Arr,
		const std::vector<Vector> & W1perpArr) {
	// read out number of samples and number of timesteps
	long M = W1Arr.size();
	int N = W1Arr[0].size();
	// generate vectors of increments along a path
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// generate return variable for all prices along all trajectories
	std::vector<Vector> payoffArr(M, Vector(par.size())); // matrix of size M x par.size()

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(numThreads);

	#pragma omp parallel \
		firstprivate(W1, Wtilde, WtildeScaled, v, GammaMap), \
		private(Ivdt, IsvdW, dt, sdt), shared(payoffArr, W1Arr, W1perpArr) //W1Cont, W1perpCont,
	{
	#pragma omp for schedule(static)
		for (int m = 0; m < M; ++m) {
			W1 = W1Arr[m];
			W1perp = W1perpArr[m];
			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {
				double payoff =  updatePayoff(par, xi, i, Wtilde, WtildeScaled, W1, W1perp, v,
										GammaMap, fft, nDFT, N, omp_get_thread_num());

				// collect payoff
				payoffArr[m][i] = payoff;
			}
		}
	}

	return payoffArr;
}

std::vector<Vector> ComputePayoffRTsamples_ST(double xi, Vector H, Vector eta,
		Vector rho, Vector T, Vector K, const std::vector<Vector> & W1Arr,
		const std::vector<Vector> & W1perpArr) {
	// read out number of samples and number of timesteps
	const int numThreads = 1;
	long M = W1Arr.size();
	int N = W1Arr[0].size();
	// generate vectors of increments along a path
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// generate return variable for all prices along all trajectories
	std::vector<Vector> payoffArr(M, Vector(par.size())); // matrix of size M x par.size()

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// The big loop which needs to be parallelized in future
	// rng is firstprivate because of the shared dist
	// Also try what happens if rng is shared... Has negligible effect on MAC, but does not make it work in WIAS
	for (int m = 0; m < M; ++m) {
		// generate the fundamental Gaussians
		W1 = W1Arr[m];
		W1perp = W1perpArr[m];

		// now iterate through all parameters
		for (long i = 0; i < par.size(); ++i) {

			// Note that each of the changes here forces all subsequent updates!
			// check if H has changed. If so, Wtilde needs to be updated (and, hence, everything else)
			bool update = par.HTrigger(i);
			if (update)
				updateWtilde(Wtilde, W1, W1perp, par.H(i), GammaMap, fft,nDFT);
			// check if T has changed. If so, Wtilde and the time increment need re-scaling
			update = update || par.TTrigger(i);
			if (update) {
				scaleWtilde(WtildeScaled, Wtilde, par.T(i), par.H(i));
				dt = par.T(i) / N;
				sdt = sqrt(dt);
			}
			// check if eta has changed. If so, v needs to be updated
			update = update || par.etaTrigger(i);
			if (update)
				updateV(v, WtildeScaled, xi, par.H(i), par.eta(i), dt);
			// now compute \int v_s ds, \int \sqrt{v_s} dW_s with W = W1
			Ivdt = intVdt(v, dt);
			IsvdW = intRootVdW(v, W1, sdt);

			// now compute the payoff by inserting properly into the BS formula
			double BS_vol = sqrt((1.0 - par.rho(i) * par.rho(i)) * Ivdt);
			double BS_spot = exp(
					-0.5 * par.rho(i) * par.rho(i) * Ivdt + par.rho(i) * IsvdW);
			double payoff = BS_call_price(BS_spot, par.K(i), 1.0, BS_vol);

			//double payoff =  updatePayoff(par, xi, i, Wtilde, WtildeScaled, W1, W1perp, v,
			//						GammaMap, fft, nDFT, N, omp_get_thread_num());

			// collect payoff
			payoffArr[m][i] = payoff;
		}
	}

	return payoffArr;
}

Result ComputePriceRTMT_sobol(double xi, Vector H, Vector eta, Vector rho, Vector T, Vector K, int N, long M,
			int numThreads){
	// The random vectors; the first 3 are independent, Z is a composite
	// Note that W1, W1perp, Wperp, Z correspond to UNNORMALIZED increments of Brownian motions,
	// i.e., are i.i.d. standard normal.
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);
	Vector Z(2*N); // joint vector for W1 and W1perp
	// In the sense of a hierarchical representation, we put the even entries of Z
	// into W1 and the odd ones into W1perp. This is only a guess!!!!

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// vectors of prices and variances
	Vector price(par.size(), 0.0);
	Vector var(par.size(), 0.0);

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// gather the RNG
	//RNG rng(numThreads, seed);

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(numThreads);

	// The big loop which needs to be parallelized in future
	#pragma omp parallel \
		firstprivate(W1, Wtilde, WtildeScaled, v, Z, GammaMap), \
		private(Ivdt, IsvdW, dt, sdt)
	{
		Vector price_private(par.size(), 0.0);
		Vector var_private(par.size(), 0.0);
		#pragma omp for
		for (int m = 0; m < M; ++m) {
			// generate the fundamental Gaussians; The underlying Sobol'-code is not thread-safe, so...
			#pragma omp critical
			{
				normalQMC_sample(Z, 2*N, m+1); // m = 0 hits the singularity!
			}
			breakZ(Z, W1, W1perp);
			//genGaussianMT(W1, rng, omp_get_thread_num());
			//genGaussianMT(W1perp, rng, omp_get_thread_num());

			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {
				double payoff =  updatePayoff(par, xi, i, Wtilde, WtildeScaled, W1, W1perp, v,
										GammaMap, fft, nDFT, N, omp_get_thread_num());

				price_private[i] += payoff;
				var_private[i] += payoff * payoff;
			}
		}
		// now sum all the prices and variances from the individual threads
		#pragma omp critical
		{
			for (long i = 0; i < par.size(); ++i) {
				price[i] += price_private[i];
				var[i] += var_private[i];
			}
		}
	}

	// compute mean and variance
	scaleVector(price, 1.0 / double(M));
	scaleVector(var, 1.0 / double(M)); // = E[X^2]
	var = linearComb(1.0, var, -1.0, squareVector(price)); // = empirical var of price
	Vector stat = rootVector(var);
	scaleVector(stat, 1.0 / sqrt(double(M)));
	Vector iv(par.size());
	Result res { price, iv, par, stat, N, M, numThreads, 0.0 };

	return res;
}

Result ComputeIVRTMT_sobol(double xi, Vector H, Vector eta, Vector rho, Vector T, Vector K, int N, long M,
			int numThreads){
	Result res =
			ComputePriceRTMT_sobol(xi, H, eta, rho, T, K, N, M, numThreads);
	// Compute the implied vol for each price saved in res
	for (long i = 0; i < res.par.size(); ++i) {
		res.iv[i] = IV_call(res.price[i], 1.0, res.par.K(i), res.par.T(i));
	}
	return res;
}

Result ComputePriceRTST_sobol(double xi, Vector H, Vector eta, Vector rho, Vector T, Vector K, int N, long M){
	// The random vectors; the first 3 are independent, Z is a composite
	// Note that W1, W1perp, Wperp, Z correspond to UNNORMALIZED increments of Brownian motions,
	// i.e., are i.i.d. standard normal.
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);
	Vector Z(2*N); // joint vector for W1 and W1perp
	// In the sense of a hierarchical representation, we put the even entries of Z
	// into W1 and the odd ones into W1perp. This is only a guess!!!!

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// vectors of prices and variances
	Vector price(par.size(), 0.0);
	Vector var(par.size(), 0.0);

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, 1);

	// gather the RNG
	//RNG rng(numThreads, seed);

	{
		Vector price_private(par.size(), 0.0);
		Vector var_private(par.size(), 0.0);
		for (int m = 0; m < M; ++m) {
			// generate the fundamental Gaussians
			normalQMC_sample(Z, 2*N, m+1);
			breakZ(Z, W1, W1perp);
			//genGaussianMT(W1, rng, omp_get_thread_num());
			//genGaussianMT(W1perp, rng, omp_get_thread_num());

			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {
				double payoff =  updatePayoff(par, xi, i, Wtilde, WtildeScaled, W1, W1perp, v,
						GammaMap, fft, nDFT, N, omp_get_thread_num());

				price_private[i] += payoff;
				var_private[i] += payoff * payoff;
			}
		}
		{
			for (long i = 0; i < par.size(); ++i) {
				price[i] += price_private[i];
				var[i] += var_private[i];
			}
		}
	}

	// compute mean and variance
	scaleVector(price, 1.0 / double(M));
	scaleVector(var, 1.0 / double(M)); // = E[X^2]
	var = linearComb(1.0, var, -1.0, squareVector(price)); // = empirical var of price
	Vector stat = rootVector(var);
	scaleVector(stat, 1.0 / sqrt(double(M)));
	Vector iv(par.size());
	Result res { price, iv, par, stat, N, M, 1, 0.0 };

	return res;
}

Result ComputeIVRTST_sobol(double xi, Vector H, Vector eta, Vector rho, Vector T, Vector K, int N, long M){
	Result res =
			ComputePriceRTST_sobol(xi, H, eta, rho, T, K, N, M);
	// Compute the implied vol for each price saved in res
	for (long i = 0; i < res.par.size(); ++i) {
		res.iv[i] = IV_call(res.price[i], 1.0, res.par.K(i), res.par.T(i));
	}
	return res;
}

void parallelSobol(int dim, int M){
	Vector Z(dim);
	std::cout << "Print out the QMC samples in a single-threaded code:\n";
	for(int i=0; i<M; ++i){
		normalQMC_sample(Z, dim, i+1);
		std::cout << "Z[" << i << "] = " << Z << std::endl;
	}

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(8);
	std::cout << "\n\nPrint out the QMC samples in a multi-threaded code:\n";
	int seed = 0;
	#pragma omp parallel firstprivate(Z, seed), shared(M, dim)
	{
		#pragma omp for
		for(int i = 0; i<M; ++i){
			#pragma omp critical
			{
				normalQMC_sample(Z, dim, i+1);
			}
			#pragma omp critical
			{
				std::cout << "Z[" << i << "] = " << Z << std::endl;
			}
		}
	}
}

ResultUnordered ComputePriceRTMTunstructured(double xi, Vector H, Vector eta, Vector rho, Vector T, Vector K, int N, long M,
		int numThreads, std::vector<uint64_t> seed){
	// The random vectors; the first 3 are independent, Z is a composite
	// Note that W1, W1perp, Wperp, Z correspond to UNNORMALIZED increments of Brownian motions,
	// i.e., are i.i.d. standard normal.
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTotUnordered par(H, eta, rho, T, K, xi);

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// vectors of prices and variances
	Vector price(par.size(), 0.0);
	Vector var(par.size(), 0.0);

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// gather the RNG
	RNG rng(numThreads, seed);

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(numThreads);

	// The big loop which needs to be parallelized in future
	// rng is firstprivate because of the shared dist
	// Also try what happens if rng is shared... Has negligible effect on MAC, but does not make it work in WIAS
	#pragma omp parallel \
		firstprivate(W1, Wtilde, WtildeScaled, v, rng, GammaMap), \
		private(Ivdt, IsvdW, dt, sdt)
	{
		Vector price_private(par.size(), 0.0);
		Vector var_private(par.size(), 0.0);
		#pragma omp for
		for (int m = 0; m < M; ++m) {
			// generate the fundamental Gaussians
			genGaussianMT(W1, rng, omp_get_thread_num());
			genGaussianMT(W1perp, rng, omp_get_thread_num());

			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {
				double payoff = updatePayoffUnordered(par, xi, i, Wtilde, WtildeScaled, W1,
						W1perp, v, GammaMap, fft, nDFT, N, omp_get_thread_num());

				price_private[i] += payoff;
				var_private[i] += payoff * payoff;
			}
		}
		// now sum all the prices and variances from the individual threads
		#pragma omp critical
		{
			for (long i = 0; i < par.size(); ++i) {
				price[i] += price_private[i];
				var[i] += var_private[i];
			}
		}
	}

	// compute mean and variance
	scaleVector(price, 1.0 / double(M));
	scaleVector(var, 1.0 / double(M)); // = E[X^2]
	var = linearComb(1.0, var, -1.0, squareVector(price)); // = empirical var of price
	Vector stat = rootVector(var);
	scaleVector(stat, 1.0 / sqrt(double(M)));
	Vector iv(par.size());
	ResultUnordered res { price, iv, par, stat, N, M, numThreads, 0.0 };

	return res;
}

ResultUnordered ComputeIVRTMTunstructured(double xi, Vector H, Vector eta, Vector rho, Vector T, Vector K, int N, long M,
		int numThreads, std::vector<uint64_t> seed){
	ResultUnordered res =
			ComputePriceRTMTunstructured(xi, H, eta, rho, T, K, N, M, numThreads, seed);
	// Compute the implied vol for each price saved in res
	for (long i = 0; i < res.par.size(); ++i) {
		res.iv[i] = IV_call(res.price[i], 1.0, res.par.K(i), res.par.T(i));
	}
	return res;
}

double updatePayoffUnordered(ParamTotUnordered& par, double xi, long i, Vector& Wtilde,
		Vector& WtildeScaled, Vector& W1, Vector& W1perp, Vector& v,
		std::map<double, Vector>& GammaMap, fftData& fft, int nDFT, int N, int threadID){
	updateWtilde(Wtilde, W1, W1perp, par.H(i), GammaMap, fft, nDFT);
	scaleWtilde(WtildeScaled, Wtilde, par.T(i), par.H(i));
	double dt = par.T(i) / N;
	double sdt = sqrt(dt);
	updateV(v, WtildeScaled, xi, par.H(i), par.eta(i), dt);
	double Ivdt = intVdt(v, dt);
	double IsvdW = intRootVdW(v, W1, sdt);

	// now compute the payoff by inserting properly into the BS formula
	double BS_vol = sqrt((1.0 - par.rho(i) * par.rho(i)) * Ivdt);
	double BS_spot = exp(
			-0.5 * par.rho(i) * par.rho(i) * Ivdt + par.rho(i)
					* IsvdW);
	double payoff = BS_call_price(BS_spot, par.K(i), 1.0, BS_vol);

	return payoff;
}

double updatePayoff(const ParamTot& par, double xi, long i, Vector& Wtilde,
		Vector& WtildeScaled, const Vector& W1, const Vector& W1perp, Vector& v,
		const std::map<double, Vector>& GammaMap, fftData& fft, int nDFT, int N, int threadID){
		// Note that each of the changes here forces all subsequent updates!
		// check if H has changed. If so, Wtilde needs to be updated (and, hence, everything else)
		bool update = par.HTrigger(i);
		// Bugfix:
		/*
		#pragma omp critical
		{
		std::cout << "update = " << update
				<< "\nW1 = (" << W1[0] << ", " << W1[1] << ",...)"
				<<  "\nW1perp = (" << W1perp[0] << ", " << W1perp[1] << ",...)"
				<< "\nH = " << par.H(i)
				<< "\nThread = " << threadID << '\n';
		}
		*/
		if (update)
			updateWtilde(Wtilde, W1, W1perp, par.H(i), GammaMap, fft, nDFT);
		// check if T has changed. If so, Wtilde and the time increment need re-scaling
		update = update || par.TTrigger(i);
		if (update) {
			scaleWtilde(WtildeScaled, Wtilde, par.T(i), par.H(i));
		}
		double dt = par.T(i) / N;
		double sdt = sqrt(dt);
		// check if eta has changed. If so, v needs to be updated
		update = update || par.etaTrigger(i);
		if (update)
			updateV(v, WtildeScaled, xi, par.H(i), par.eta(i), dt);
		// now compute \int v_s ds, \int \sqrt{v_s} dW_s with W = W1
		double Ivdt = intVdt(v, dt);
		double IsvdW = intRootVdW(v, W1, sdt);

		// now compute the payoff by inserting properly into the BS formula
		double BS_vol = sqrt((1.0 - par.rho(i) * par.rho(i)) * Ivdt);
		double BS_spot = exp(
				-0.5 * par.rho(i) * par.rho(i) * Ivdt + par.rho(i)
						* IsvdW);
		double payoff = BS_call_price(BS_spot, par.K(i), 1.0, BS_vol);

		return payoff;
}

// Test the updatePayoff function.
Result test_updatePayoff(double xi, Vector H, Vector eta, Vector rho, Vector T,
		Vector K, int N, long M, int numThreads, std::vector<uint64_t> seed) {
	// The random vectors; the first 3 are independent, Z is a composite
	// Note that W1, W1perp, Wperp, Z correspond to UNNORMALIZED increments of Brownian motions,
	// i.e., are i.i.d. standard normal.
	Vector W1(N);
	Vector W1perp(N);
	Vector Wtilde(N);
	Vector WtildeScaled(N); // Wtilde scaled according to time
	Vector v(N);
	Vector Wtilde_new(N);
	Vector WtildeScaled_new(N); // Wtilde scaled according to time
	Vector v_new(N);

	double Ivdt, IsvdW; // \int v_s ds, \int \sqrt{v_s} dW_s; Here, W = W1; // maybe it is better to use a vector of S's corresponding to all different maturities!!
	// This would need a major re-organization of the code, including ParamTot...

	// The vectors of all combinations of parameter values and prices
	ParamTot par(H, eta, rho, T, K, xi);

	// A map of all vectors Gamma for all the different values of H.
	std::map<double, Vector> GammaMap;
	Vector GammaVec(N);
	for (size_t i = 0; i < H.size(); ++i) {
		getGamma(GammaVec, H[i]);
		GammaMap[H[i]] = GammaVec;
	}

	// vectors of prices and variances
	Vector price(par.size(), 0.0);
	Vector var(par.size(), 0.0);

	// other parameters used
	double dt; // time increment
	double sdt; // square root of time increment

	// gather the data needed for the FFT
	int nDFT = 2 * N - 1;
	fftData fft(nDFT, numThreads);

	// The data for the alternative FFT procedure
	std::vector<ConvolveFFTW> convFFTW(numThreads);
	std::vector<ConvolveGSL> convGSL(numThreads);
	std::vector<Convolve*>  conv(numThreads);
	for(int i =0; i<numThreads; ++i){
		convFFTW[i] = ConvolveFFTW(N);
		convGSL[i] = ConvolveGSL(N);
		conv[i] = &convGSL[i];
	}

	// gather the RNG
	RNG rng(numThreads, seed);

	// Enforce that OMP use numThreads threads
	omp_set_dynamic(0);     // Explicitly disable dynamic teams
	omp_set_num_threads(numThreads);

	// The big loop which needs to be parallelized in future
	// rng is firstprivate because of the shared dist
	// Also try what happens if rng is shared... Has negligible effect on MAC, but does not make it work in WIAS
	#pragma omp parallel \
		firstprivate(W1, Wtilde, Wtilde_new, WtildeScaled, WtildeScaled_new, v, v_new, rng, GammaMap), \
		private(Ivdt, IsvdW, dt, sdt)
	{
		Vector price_private(par.size(), 0.0);
		Vector var_private(par.size(), 0.0);
		#pragma omp for
		for (int m = 0; m < M; ++m) {
			// generate the fundamental Gaussians
			//genGaussianMT(W1, rng, omp_get_thread_num());
			//genGaussianMT(W1perp, rng, omp_get_thread_num());
			debugFillVector(W1, m);
			debugFillVector(W1perp, m);


			// now iterate through all parameters
			for (long i = 0; i < par.size(); ++i) {


				// Note that each of the changes here forces all subsequent updates!
				// check if H has changed. If so, Wtilde needs to be updated (and, hence, everything else)
				bool update = par.HTrigger(i);
/*
				#pragma omp critical
				{
					std::cout << "update = " << update
							<< "\nW1 = (" << W1[0] << ", " << W1[1] << ",...)"
							<<  "\nW1perp = (" << W1perp[0] << ", " << W1perp[1] << ",...)"
							<< "\nH = " << par.H(i)
							<< "\nThread = " << omp_get_thread_num() << '\n';
				}
*/
				if (update)
					//updateWtilde(Wtilde, W1, W1perp, par.H(i), GammaMap, fft, nDFT);
					updateWtildeConv(Wtilde, W1, W1perp, par.H(i), GammaMap, conv[omp_get_thread_num()]);

/*
				#pragma omp critical
{
	std::cout << "\nWtilde = " << Wtilde << '\n';
}
*/
				// check if T has changed. If so, Wtilde and the time increment need re-scaling

				update = update || par.TTrigger(i);
				if (update) {
					scaleWtilde(WtildeScaled, Wtilde, par.T(i), par.H(i));
					dt = par.T(i) / N;
					sdt = sqrt(dt);
				}
				// check if eta has changed. If so, v needs to be updated
				update = update || par.etaTrigger(i);
				if (update)
					updateV(v, WtildeScaled, xi, par.H(i), par.eta(i), dt);
				// now compute \int v_s ds, \int \sqrt{v_s} dW_s with W = W1
				Ivdt = intVdt(v, dt);
				IsvdW = intRootVdW(v, W1, sdt);

				// now compute the payoff by inserting properly into the BS formula
				double BS_vol = sqrt((1.0 - par.rho(i) * par.rho(i)) * Ivdt);
				double BS_spot = exp(
						-0.5 * par.rho(i) * par.rho(i) * Ivdt + par.rho(i)
								* IsvdW);
				double payoff_old = BS_call_price(BS_spot, par.K(i), 1.0, BS_vol);


				double payoff_new =  updatePayoff(par, xi, i, Wtilde_new, WtildeScaled_new, W1, W1perp, v_new,
														GammaMap, fft, nDFT, N, omp_get_thread_num());

				// Print old and new payoff
				# pragma omp critical
				{
					if(fabs(payoff_old - payoff_new) > 0.00000001)
					{
						/*
						if(fabs(payoff_old - payoff_new) > 0.00000001){
							std::cout << "\033[1;31mDifference detected!\033[0m\n";
							std::cout << "Wtilde - Wtilde_new = " << linearComb(1.0, Wtilde, -1.0, Wtilde_new) << "\n";
						}
						else{
							std::cout << "\033[1;32mNo difference detected!\033[0m\n";
						}
						*/
						//std::cout << "m = " << m << ", threadID = " << omp_get_thread_num() << ", i = " << i << "\n";
						//std::cout << "Payoff (old) = " << payoff_old << ", payoff (new) = " << payoff_new << "\n";
						std::cout << m << " " << payoff_old << " " << payoff_new << '\n';
						/*std::cout << "For old code: Ivdt = " << Ivdt << ", IsvdW = " << IsvdW << "\nBS_vol = " << BS_vol
								<< ", BS_spot = " << BS_spot
								<< "\nv = " << v
								<< "\ndt = " << dt
								<< "\n\n";*/
					}
					// No NaN's appear when I use just 1 thread -> seems to be a multi-threading issue!
					// Note: NaN started to appear in the old payoff when v (as constructed in the new function)
					// did not need to be updated. Let us next fully separate old and new variables.
					// NaN-BUG RESOLVED!
					// Bug: payoff_old and payoff_new routinely give different values.
					// Note: Bug only appears when numThreads > 1.
					// Check if it happens all the time, or only sometimes.
					// The bug does not happen always. Note: payoff == 0 does not happen due to Romano-Touzi!
					// Differences seem to come in batches...
					// Changing the order between the function calls does not seem to matter.
					// When the difference happens, it already leads to v - v_new != 0, otherwise they are equal.
					// When the difference happens, it already leads to Wtilde - Wtilde_new != 0, otherwise they are equal.
					// Only seems to happen when i == 0 or i == 1, but not every time.
					// Next step: Figure out, which one is correct by comparing with results from 1 thread.
					// In all observed cases for the deterministic example, the old code got it right, when the old code was
					// called before the new one. Changing the order, the new code was correct.
					// TODO: Tomorrow, implement the cubature version in the single-threaded class and
					// then compare for the input given by debugFillVector!!!
					// Comparison with single-threaded code:
					// 1) (payoff_new before payoff_old): 20 cases run, 3 cases with diverging results, payoff_new correct 2 times,
					//		the last time neither were correct.
					// 2) (Payoff_old before payoff_new): 20 cases run, 4 cases with diverging results, payoff_old correct 4 times.
					// Next run more extensive tests for only payoff_new and compare with ST code.
					// Out of 200 samples, the multi-threaded code was wrong 41 times. Hence, the multi-threaded code is not trustworthy.
					// Test for payoff_old: Out of 200 samples, the old version of the multi-threaded code was wrong 45 times.
					// Replace "threadID" everywhere by calling omp_get_thread_num()
					// The problem seems to persist even when using the GSL-FFT instead of FFTW. Maybe it is more complicated than
					// expected?
				}

				price_private[i] += payoff_old;
				var_private[i] += payoff_old * payoff_old;
			}
		}
		// now sum all the prices and variances from the individual threads
		#pragma omp critical
		{
			for (long i = 0; i < par.size(); ++i) {
				price[i] += price_private[i];
				var[i] += var_private[i];
			}
		}
	}

	// compute mean and variance
	scaleVector(price, 1.0 / double(M));
	scaleVector(var, 1.0 / double(M)); // = E[X^2]
	var = linearComb(1.0, var, -1.0, squareVector(price)); // = empirical var of price
	Vector stat = rootVector(var);
	scaleVector(stat, 1.0 / sqrt(double(M)));
	Vector iv(par.size());
	Result res { price, iv, par, stat, N, M, numThreads, 0.0 };

	return res;
}
