#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/eigen.h>
#include <pybind11/stl_bind.h>
#include <pybind11/iostream.h>

#include <memory>
#include <string>
#include <vector>

#include "FitSubroutines.h"
#include "Instrument.h"
#include "TPVMap.h"
#include "WCSFoFRoutine.h"
#include "WCSFitRoutine.h"

using namespace pybind11::literals;
namespace py = pybind11;

template <class T = float>
void declareBounds(py::module &m) {
    using Class = Bounds<T>;

    py::class_<Class>(m, "Bounds").def(py::init<const T, const T, const T, const T>());
}

template <typename T>
py::array_t<T> vectorToNumpy(std::vector<T> const &vect) {
        py::array_t<T> result(vect.size());
        std::copy (vect.begin(), vect.end(), result.mutable_data(0));
        return result;
}

template <typename T>
py::array_t<T> vectorToNumpy(std::vector<std::vector<T>> const &vect) {
        if (vect.size() == 0) {
                std::array<std::size_t, 2> shape = {0, 0};
                py::array_t<T> result(shape);
                return result;
        }
        else {
                std::array<std::size_t, 2> shape = {vect.size(), vect[0].size()};
                py::array_t<T> result(shape);
                for (std::size_t i=0; i != vect.size(); ++i) {
                        std::copy (vect[i].begin(), vect[i].end(), result.mutable_data(i, 0));
                }
                return result;
        }
}

PYBIND11_MODULE(wcsfit, m) {
    m.attr("DEGREE") = DEGREE;
    m.attr("ARCSEC") = ARCSEC;
    m.attr("EclipticInclination") = EclipticInclination;

    declareBounds<double>(m);

    py::class_<img::FTable>(m, "FTable")
            .def(py::init<long>())
            .def("addColumnDouble", &img::FTable::addColumn<double>, py::arg("values"), py::arg("columnName"),
                 py::arg("repeat") = -1, py::arg("stringLength") = -1)
            .def("addColumnStr", &img::FTable::addColumn<std::string>, py::arg("values"),
                 py::arg("columnName"), py::arg("repeat") = -1, py::arg("stringLength") = -1);

    //////// Classes needed for setting up the WCS //////////

    py::class_<astrometry::PixelMap>(m, "PixelMap");

    py::class_<astrometry::SphericalCoords>(m, "SphericalCoords");

    py::class_<astrometry::SphericalICRS, astrometry::SphericalCoords>(m, "SphericalICRS")
            .def(py::init<>())
            .def(py::init<double, double>());

    py::class_<astrometry::Gnomonic, astrometry::SphericalCoords>(m, "Gnomonic")
            .def(py::init([](double ra, double dec) {
                astrometry::SphericalICRS pole(ra, dec);
                astrometry::Orientation orientIn(pole);
                return astrometry::Gnomonic(orientIn.getPole(), orientIn);
            }))
            .def("getOrientMatrix", [](astrometry::Gnomonic &self) {
                const astrometry::Orientation* orient=self.getOrient();
                if (orient == nullptr) {
                    throw std::runtime_error("Gnomonic projection does not have orientation set");
                }
                return orient->m();
            });

    py::class_<astrometry::LinearMap, astrometry::PixelMap>(m, "LinearMap")
            .def(py::init<astrometry::DVector::Base const &>());

    py::class_<poly2d::Poly2d>(m, "Poly2d")
            .def(py::init<int>())
            .def(py::init<astrometry::DMatrix::Base const &>())
            .def("nCoeffs", &poly2d::Poly2d::nCoeffs)
            .def("getC", &poly2d::Poly2d::getC)
            .def("setC", &poly2d::Poly2d::setC)
            .def("vectorIndex", &poly2d::Poly2d::vectorIndex);

    py::class_<astrometry::PolyMap, astrometry::PixelMap>(m, "PolyMap")
            .def(py::init<poly2d::Poly2d, poly2d::Poly2d, std::string, Bounds<double>, double>());

    py::class_<astrometry::SubMap, astrometry::PixelMap>(m, "SubMap")
            .def(py::init<list<astrometry::PixelMap *> const &, std::string, bool>());

    py::class_<astrometry::ASTMap, astrometry::PixelMap>(m, "ASTMap")
            .def(py::init<ast::Mapping const &, std::string>(),
                 py::arg("mapping_"), py::arg("name_") = "");

    py::class_<astrometry::Wcs, std::shared_ptr<astrometry::Wcs>>(m, "Wcs")
            .def(py::init<astrometry::PixelMap *, astrometry::SphericalCoords const &, std::string, double,
                          bool>(),
                 py::arg("pm_"), py::arg("nativeCoords_"), py::arg("name") = "", py::arg("wScale_") = DEGREE,
                 py::arg("shareMap_") = false)
            .def("reprojectTo", &astrometry::Wcs::getTargetCoords)
            .def("getTargetCoords", &astrometry::Wcs::getTargetCoords)
            .def("getNativeCoords", &astrometry::Wcs::getNativeCoords)
            .def("toWorld", [](astrometry::Wcs &self, double x, double y) {
                double ra;
                double dec;
                self.toWorld(x, y, ra, dec);
                std::vector<double> radec = {ra, dec};
                return radec;
            });

    m.def("readWCSs", &readWCSs);
    m.def("readTPVFromSIP", &astrometry::readTPVFromSIP, py::arg("header"), py::arg("name") = "");

    /////////// Data Classes /////////////////
    py::class_<Fields>(m, "Fields")
            .def(py::init<std::vector<std::string>, std::vector<double>, std::vector<double>,
                          std::vector<double>>());

    py::class_<Instrument, std::shared_ptr<Instrument>>(m, "Instrument")
            .def(py::init<std::string>(), py::arg("name_") = "")
            .def_readwrite("name", &Instrument::name)
            .def_readwrite("band", &Instrument::band)
            .def("addDevice", &Instrument::addDevice)
            .def("hasDevice", [](Instrument &self, std::string name) { return self.deviceNames.has(name); });

    py::class_<ExposuresHelper>(m, "ExposuresHelper")
            .def(py::init<std::vector<std::string>, std::vector<int>, std::vector<int>, std::vector<double>,
                          std::vector<double>, std::vector<double>, std::vector<double>,
                          std::vector<double>, std::vector<std::vector<double>>>())
            .def_readwrite("instrumentNumbers", &ExposuresHelper::instrumentNumbers)
            .def_readwrite("fieldNumbers", &ExposuresHelper::fieldNumbers);

    py::class_<astrometry::YAMLCollector>(m, "YAMLCollector")
            .def(py::init<std::string, std::string>())
            .def(
                    "addInput",
                    [](astrometry::YAMLCollector &self, std::string is, std::string filter, bool prepend) {
                        std::istringstream iss(is);
                        self.addInput(iss, filter, prepend);
                    },
                    py::arg("is"), py::arg("filter") = "", py::arg("prepend") = false);

    py::class_<astrometry::IdentityMap, astrometry::PixelMap>(m, "IdentityMap")
            .def(py::init<>())
            .def("getName", &astrometry::IdentityMap::getName);

    py::class_<ExtensionObjectSet>(m, "ExtensionObjectSet").def(py::init<std::string>());

    py::class_<astrometry::PixelMapCollection>(m, "PixelMapCollection")
            .def("allMapNames", &astrometry::PixelMapCollection::allMapNames)
            .def("getMapType", &astrometry::PixelMapCollection::getMapType)
            .def("dependencies", &astrometry::PixelMapCollection::dependencies)
            .def("orderAtoms", &astrometry::PixelMapCollection::orderAtoms)
            .def("getParamDict", &astrometry::PixelMapCollection::getParamDict)
            .def("getWcsNativeCoords", &astrometry::PixelMapCollection::getWcsNativeCoords,
                 py::arg("wcsName"), py::arg("degrees") = false)
            .def("dependencies", &astrometry::PixelMapCollection::dependencies);

    ////////////// WCS-fitting class ////////////////////////
    py::class_<WCSFit>(m, "WCSFit")
            .def(py::init<Fields &, std::vector<std::shared_ptr<Instrument>>, ExposuresHelper,
                          std::vector<int>, std::vector<int>, astrometry::YAMLCollector,
                          std::vector<std::shared_ptr<astrometry::Wcs>>, std::vector<int>,
                          std::vector<LONGLONG>, std::vector<LONGLONG>, std::vector<int>, double, double, int,
                          std::string, std::string, bool, double, double, int>(),
                 py::arg("fields"), py::arg("instruments"), py::arg("exposures"),
                 py::arg("extensionExposureNumbers"), py::arg("extensionDevices"), py::arg("inputYAML"),
                 py::arg("wcss"), py::arg("sequence"), py::arg("extns"), py::arg("objects"),
                 py::arg("exposureColorPriorities") = std::vector<int>(), py::arg("sysErr") = 2.0,
                 py::arg("refSysErr") = 2.0, py::arg("minMatches") = 2, py::arg("skipObjectsFile") = "",
                 py::arg("fixMaps") = "", py::arg("usePM") = true, py::arg("pmPrior") = 100.0,
                 py::arg("parallaxPrior") = 10.0, py::arg("verbose") = 0)
            .def("setObjects", &WCSFit::setObjects, py::arg("i"), py::arg("tableMap"), py::arg("xKey"),
                 py::arg("yKey"), py::arg("xyErrKeys"), py::arg("idKey") = "", py::arg("pmCovKey") = "",
                 py::arg("magKey") = "", py::arg("magKeyElement") = 0, py::arg("magErrKey") = "",
                 py::arg("magErrKeyElement") = 0, py::arg("pmRaKey") = "", py::arg("pmDecKey") = "",
                 py::arg("parallaxKey") = "", py::arg("pmCov") = std::vector<std::vector<double>>(0.0),
                 py::arg("defaultColor") = 0)
            .def("setColors", &WCSFit::setColors, py::arg("matchIDs"), py::arg("colors"))
            .def_readwrite("verbose", &WCSFit::verbose)
            .def("setMatches", &WCSFit::setMatches)
            .def("fit", &WCSFit::fit, py::arg("maxError") = 100., py::arg("minFitExposures") = 200,
                 py::arg("reserveFraction") = 0.2, py::arg("randomNumberSeed") = 1234,
                 py::arg("minimumImprovement") = 0.02, py::arg("clipThresh") = 5.0,
                 py::arg("chisqTolerance") = 0.001, py::arg("clipEntireMatch") = false,
                 py::arg("divideInPlace") = false, py::arg("purgeOutput") = false,
                 py::arg("minColor") = -10.0, py::arg("maxColor") = 10.0, py::arg("calcSVD") = false)
            .def("saveResults", &WCSFit::saveResults)
            .def("getOutputCatalog", [](WCSFit &self) {
                    Astro::outputCatalog outCat = self.getOutputCatalog();
                    py::dict result;
                    result["matchID"] = vectorToNumpy(outCat.matchID);
                    result["catalogNumber"] = vectorToNumpy(outCat.catalogNumber);
                    result["objectNumber"] = vectorToNumpy(outCat.objectNumber);
                    result["exposureName"] = outCat.exposureName;
                    result["deviceName"] = outCat.deviceName;
                    result["mjd"] = vectorToNumpy(outCat.mjd);
                    result["clip"] = vectorToNumpy(outCat.clip);
                    result["reserve"] = vectorToNumpy(outCat.reserve);
                    result["hasPM"] = vectorToNumpy(outCat.hasPM);
                    result["color"] = vectorToNumpy(outCat.color);
                    result["xresw"] = vectorToNumpy(outCat.xresw);
                    result["yresw"] = vectorToNumpy(outCat.yresw);
                    result["xpix"] = vectorToNumpy(outCat.xpix);
                    result["ypix"] = vectorToNumpy(outCat.ypix);
                    result["sigpix"] = vectorToNumpy(outCat.sigpix);
                    result["xrespix"] = vectorToNumpy(outCat.xrespix);
                    result["yrespix"] = vectorToNumpy(outCat.yrespix);
                    result["xworld"] = vectorToNumpy(outCat.xworld);
                    result["yworld"] = vectorToNumpy(outCat.yworld);
                    result["chisq"] = vectorToNumpy(outCat.chisq);
                    result["chisqExpected"] = vectorToNumpy(outCat.chisqExpected);
                    result["covTotalW_00"] = vectorToNumpy(outCat.covTotalW_00);
                    result["covTotalW_11"] = vectorToNumpy(outCat.covTotalW_11);
                    result["covTotalW_01"] = vectorToNumpy(outCat.covTotalW_01);
                    return result;
            })
            .def("getPMCatalog", [](WCSFit &self) {
                    Astro::PMCatalog pmCat = self.getPMCatalog();
                    py::dict result;
                    result["pmMatchID"] = vectorToNumpy(pmCat.pmMatchID);
                    result["pmCatalogNumber"] = vectorToNumpy(pmCat.pmCatalogNumber);
                    result["pmObjectNumber"] = vectorToNumpy(pmCat.pmObjectNumber);
                    result["pmClip"] = vectorToNumpy(pmCat.pmClip);
                    result["pmReserve"] = vectorToNumpy(pmCat.pmReserve);
                    result["pmMean"] = vectorToNumpy(pmCat.pmMean);
                    result["pmInvCov"] = vectorToNumpy(pmCat.pmInvCov);
                    result["pmChisq"] = vectorToNumpy(pmCat.pmChisq);
                    result["pmChisqExpected"] = vectorToNumpy(pmCat.pmChisqExpected);
                    return result;
            })
            .def("getStarCatalog", [](WCSFit &self) {
                    Astro::StarCatalog starCat = self.getStarCatalog();
                    py::dict result;
                    result["starMatchID"] = vectorToNumpy(starCat.starMatchID);
                    result["starReserve"] = vectorToNumpy(starCat.starReserve);
                    result["starColor"] = vectorToNumpy(starCat.starColor);
                    result["starPMCount"] = vectorToNumpy(starCat.starPMCount);
                    result["starDetCount"] = vectorToNumpy(starCat.starDetCount);
                    result["starClipCount"] = vectorToNumpy(starCat.starClipCount);
                    result["starDOF"] = vectorToNumpy(starCat.starDOF);
                    result["starChisq"] = vectorToNumpy(starCat.starChisq);
                    result["starX"] = vectorToNumpy(starCat.starX);
                    result["ra"] = vectorToNumpy(starCat.starX);
                    result["starY"] = vectorToNumpy(starCat.starY);
                    result["dec"] = vectorToNumpy(starCat.starY);
                    result["starPMx"] = vectorToNumpy(starCat.starPMx);
                    result["pm_ra"] = vectorToNumpy(starCat.starPMx);
                    result["starPMy"] = vectorToNumpy(starCat.starPMy);
                    result["pm_dec"] = vectorToNumpy(starCat.starPMy);
                    result["starParallax"] = vectorToNumpy(starCat.starParallax);
                    result["parallax"] = vectorToNumpy(starCat.starParallax);
                    result["starInvCov"] = vectorToNumpy(starCat.starInvCov);
                    result["starEpoch"] = vectorToNumpy(starCat.starEpoch);
                    return result;
            })
            .def("getModelCovariance", &WCSFit::getModelCovariance)
            .def_readonly("mapCollection", &WCSFit::mapCollection);

    ///////////// Friends of Friends Fitting Class //////////////////
    py::class_<FoFClass>(m, "FoFClass")
            .def(py::init<Fields &, std::vector<std::shared_ptr<Instrument>>, ExposuresHelper,
                          std::vector<double>, double>(),
                 py::arg("fields"), py::arg("instruments"), py::arg("exposures"), py::arg("fieldExtents"),
                 py::arg("matchRadius"))
            .def("reprojectWCS", &FoFClass::reprojectWCS)
            .def("addCatalog",
                 py::overload_cast<astrometry::Wcs const &, std::string, int, int, int, int, long,
                                   std::vector<bool>, std::vector<double>, std::vector<double>,
                                   std::vector<long>>(&FoFClass::addCatalog))
            .def("sortMatches", &FoFClass::sortMatches, py::arg("fieldNumber"), py::arg("minMatches") = 2,
                 py::arg("allowSelfMatches") = false)
            .def_readwrite("sequence", &FoFClass::sequence)
            .def_readwrite("extn", &FoFClass::extn)
            .def_readwrite("obj", &FoFClass::obj);
}
