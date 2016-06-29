// Code for statistics accumulator.
// Templated to either Astro or Photo, with
// appropriate specializations in its compilation.
// The Photo case will use the "x" variable as magnitude.

#ifndef ACCUM_H
#define ACCUM_H
#include "Std.h"

template <class S>
class Accum {
public:
  double sumx;
  double sumy;
  double sumxx;
  double sumyy;
  double sumxxw;
  double sumyyw;
  double sumxw;
  double sumyw;
  double sumwx;
  double sumwy;
  double sumw;
  double sum_m;
  double sum_mw;
  double sum_mm;
  double sum_mmw;
  int n;
  double chisq;
  double sumdof;
  Accum();
  void add(const typename S::Detection* d, double xoff=0., double yoff=0., double dof=1.);
  double rms() const;
  double reducedChisq() const;
  string summary() const;
  static string header();
};


#endif
