// Make a FITS image of a DECam photometric solution, with flat normalization factors taken out.
// This version looks up star flat value from Nicolas Regnault's image that has 1024x1024 superpixels.
#include <fstream>
#include <sstream>
#include <map>

#include "StringStuff.h"

#include "FitsImage.h"
#include "PhotoMapCollection.h"
#include "PixelMapCollection.h"
using namespace img;
using namespace astrometry;
using namespace stringstuff;
using photometry::PhotoMapCollection;
using photometry::PhotoMap;

class Device {
public:
  Bounds<double> b;
  double norm;
  int detsecX;
  int detsecY;
};

int
main(int argc, char *argv[])
{
  double pixelScale = atof(argv[1]);
  string nicolasFile = argv[2];
  string outFile = argv[3];
  string exposurePrefix = "DECam_00163584/";

  // how big each of Nicolas' superpixels is, in CCD pixels
  const int superPixelSize = 1024;

  // Convert rough input scale in DECam pixels into degrees per output pix:
  pixelScale *= 0.264 / 3600.;

  // Bounds of device pixel coordinates that we will try to map:
  Bounds<double> bDevice(11., 2038., 1., 4096.);

  // Read in the device information
  map<string,Device> devices;
  Bounds<double> bExpo;
  {
    string filename = "ccdInfo.txt";
    ifstream ifs(filename.c_str());
    string buffer;
    while (getlineNoComment(ifs, buffer)) {
      istringstream iss(buffer);
      string name;
      double xmin, xmax, ymin, ymax;
      if (!(iss >> name >> xmin >> xmax >> ymin >> ymax)) {
	cerr << "Bad Device line: " << buffer << endl;
	exit(1);
      }
      Device d;
      d.b = Bounds<double>(xmin, xmax, ymin, ymax);
      bExpo += d.b;
      devices[name] = d;
    }
  }

  {
    // Read the corners of the images in detsec coordinates
    ifstream ifs("detsecs.dat");
    string buffer;
    while (getlineNoComment(ifs, buffer)) {
      string detpos;
      int detsecX, detsecY;
      istringstream iss(buffer);
      if (! (iss >> detpos >> detsecX >> detsecY)) {
	cerr << "Error on detsec reading, line: " << buffer << endl;
	exit(1);
      }
      devices[detpos].detsecX = detsecX-2048;
      devices[detpos].detsecY = detsecY;
    }
  }
  // Make the Image
  Bounds<int> bImage( static_cast<int> (floor(bExpo.getXMin()/pixelScale)),
		      static_cast<int> (ceil(bExpo.getXMax()/pixelScale)),
		      static_cast<int> (floor(bExpo.getYMin()/pixelScale)),
		      static_cast<int> (ceil(bExpo.getYMax()/pixelScale)) );
  Image<> starflat(bImage);

  // Read in astrometric and photometric solutions
  PixelMapCollection astromaps;
  {
    ifstream ifs("jul4.wcs");
    astromaps.read(ifs);
  }

  // Adjust coordinates such that mean of N4 and S4 centers is at origin.
  Position<double> center;
  {
    Bounds<double> b;
    double x,y;
    astromaps.issueWcs(exposurePrefix + "N4")->toWorld(1024.,2048.,x,y);
    b += Position<double>(x,y);
    astromaps.issueWcs(exposurePrefix + "S4")->toWorld(1024.,2048.,x,y);
    b += Position<double>(x,y);
    center = b.center();
    cout << "Center: " << center*3600. << " arcsec" << endl;
  }

  for (map<string,Device>::iterator i = devices.begin();
       i != devices.end();
       ++i) {
    // Convert device boundaries into pixel range
    Bounds<double> b = i->second.b;
    Bounds<int> bpix( static_cast<int> (floor(b.getXMin()/pixelScale)),
		      static_cast<int> (ceil(b.getXMax()/pixelScale)),
		      static_cast<int> (floor(b.getYMin()/pixelScale)),
		      static_cast<int> (ceil(b.getYMax()/pixelScale)) );

    // Get Wcs and photometric solution
    Wcs* devWcs = astromaps.issueWcs(exposurePrefix + i->first);

    // Get Nicolas's image
    Image<> nicolas;
    {
      FitsImage<> fi(nicolasFile,FITS::ReadOnly,0);
      nicolas = fi.extract();
      nicolas += 1.;
    }
    
    // Loop over pixels
    for (int iy = bpix.getYMin(); iy <= bpix.getYMax(); iy++)
      for (int ix = bpix.getXMin(); ix <= bpix.getXMax(); ix++) {
	photometry::PhotoArguments args;
	args.xExposure = ix * pixelScale + center.x;
	args.yExposure = iy * pixelScale + center.y;

	// Map pixel coordinates to exposure and device coordinates
	devWcs->toPix(args.xExposure, args.yExposure, args.xDevice, args.yDevice);
	if (!bDevice.includes(args.xDevice,args.yDevice)) continue;	// Skip if outside device

	// Find the pixel in Nicolas' image that gives superpixel here
	int xSup = (args.xDevice + i->second.detsecX-2)/superPixelSize + 1;
	int ySup = (args.yDevice + i->second.detsecY-2)/superPixelSize + 1;
	  
	starflat(ix,iy) = pow(10.,0.4*(nicolas(xSup, ySup)-1.));
      }
  } // end Device loop.
  starflat.shift(1,1);
  FitsImage<>::writeToFITS(outFile, starflat, 0);
}
