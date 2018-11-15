#include <iostream>
#include <windows.h>
#include <string>
#include <stdlib.h>

// adjust verbosity for debugging
#define VERY_VERBOSE

// tiny object loader
#define TINYOBJLOADER_IMPLEMENTATIO
#include "Libraries/tinyobj/tiny_obj_loader.h"

// alglib nearest neighbor subpackage for kdtree
#include "Libraries/alglib/alglibmisc.h"
#include "Libraries/alglib/dataanalysis.h"


namespace constants
{
	const std::string cloudPointsBasePath = "PointClouds\\";
	const unsigned int kdtTreeNormType = 2; // 2-norm (Euclidean-norm)
	const unsigned int dims = 3; // dimensions
	const unsigned int psd = 6; // precision display, used on displaying alglib f-values
}


/*
	loadCloud

		Wraps the process of loading the cloud point
		by reading vertices from the OBJ file using
		tinyobj loader.

		This can easily be extended to other formats.

		Consider CSV, PLY, etc.
*/

bool loadCloud(tinyobj::attrib_t& attrib, const std::string& filename)
{
	// dummy vectors... we won't use them, we only need vertex info
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;

	// error string buffer
	std::string err, warn;

	// load the obj file
	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, filename.c_str(), NULL, false, true);

	// display errors and warnings
	if (!err.empty()) {
		std::cerr << err << std::endl;
	}
	if (!warn.empty()) {
		std::cerr << warn << std::endl;
	}

	return ret;
}


/*
	getCloudPointFilename

		takes in the OBJ model filename
*/

std::string getCloudPointFilename(std::string basePath = constants::cloudPointsBasePath)
{
	std::string filename = "xy_nearly.obj";
	/*std::cout << "Please input cloud point filename: ";
	std::cin >> filename;*/


	return basePath + filename;
}

/*
	adaptDataPoints

		The input OBJ file is stored with
		float precisition but ALGLIB uses
		double for its R1 representations.
		
		Casting the values is ok but casting
		pointers generates missalignment.
		
		We could also set TINYOBJLOADER_USE_DOUBLE 
		to change the real_t type of tinyobj,
		however, this introduces problems
		when reading the files.
*/

alglib::real_2d_array& adaptDataPoints(const std::vector<tinyobj::real_t>& vertices, const size_t numberOfVertices, alglib::real_2d_array& points)
{
	points.setlength(numberOfVertices, constants::dims);

	for (size_t vertex = 0; vertex < numberOfVertices; vertex++)
	{
		// write d consequent values
		size_t marker = vertex * constants::dims;
		for (size_t d = 0; d < constants::dims; d++)
			points[vertex][d] = vertices[marker + d];
	}
	
	return points;
}

/*
	buildKDTree

		Build a constants::dims dimensional kdtree
		of the cloud point using norm2 
		(euclidean distance).
*/

alglib::kdtree& buildKDTree(alglib::kdtree& kdt, const alglib::real_2d_array& points, const alglib::ae_int_t n)
{
	alglib::ae_int_t xdims = constants::dims, ydims = 0, normType = constants::kdtTreeNormType;
	alglib::kdtreebuild(points, n, xdims, ydims, normType, kdt);
	return kdt;
}

alglib::kdtree& buildTaggedKDTree(alglib::kdtree& kdt, const alglib::real_2d_array& points, const alglib::integer_1d_array& tags, const alglib::ae_int_t n)
{
	alglib::ae_int_t xdims = constants::dims, ydims = 0, normType = constants::kdtTreeNormType;
	alglib::kdtreebuildtagged(points, tags, n, xdims, ydims, normType, kdt);
	return kdt;
}

/*
	getKNeighbors

		get k neighbors on a given radius of the query point
		(returns self if only point in the k-neighborhood)
*/

alglib::ae_int_t getKNeighbors(alglib::kdtree& kdt, alglib::real_2d_array& result, const alglib::real_1d_array& queryPoint, double radius)
{
	// query tree for a given point around a radius
	alglib::ae_int_t k = alglib::kdtreequeryrnn(kdt, queryPoint, radius);

	// the queried results are in an internal buffer of the tree
	result.setlength(k, constants::dims);
	alglib::kdtreequeryresultsx(kdt, result);

	// return the size of neighbor set
	return k;
}

/*
	getKNeighborsTagged

		get k neighbors on a given radius of the query point
		(returns self if only point in the k-neighborhood)
		includes tags
*/

alglib::ae_int_t getKNeighborsTagged(alglib::kdtree& kdt, alglib::real_2d_array& result, alglib::integer_1d_array& tags, const alglib::real_1d_array& queryPoint, double radius)
{
	// query tree for a given point around a radius
	alglib::ae_int_t k = alglib::kdtreequeryrnn(kdt, queryPoint, radius);

	// the queried results are in an internal buffer of the tree
	result.setlength(k, constants::dims);
	tags.setlength(k);
	
	alglib::kdtreequeryresultsx(kdt, result);
	alglib::kdtreequeryresultstags(kdt, tags);

	std::cout << tags.tostring() << std::endl;

	// return the size of neighbor set
	return k;
}

/*
	calculateCentroid

		Estimates the center to the
		best fitting plane of a set
		of points in R(constants::dims) space. 
		This centroid is the mean vector
		of the set of points.
*/

alglib::real_1d_array calculateCentroid(const alglib::real_2d_array& points, alglib::ae_int_t k)
{
	// the centroid is the mean vector of the neighbors
	alglib::real_1d_array centroid;
	centroid.setlength(constants::dims);
	
	// initialize
	for (size_t d = 0; d < constants::dims; d++)
		centroid[d] = 0;
	
	// sum mean vector
	for (alglib::ae_int_t i = 0; i < k; i++)
		for (size_t d = 0; d < constants::dims; d++)
			centroid[d] += points[i][d];

	// mean
	for (size_t d = 0; d < constants::dims; d++)
		centroid[d] /= k;

	return centroid;
}

/*
	calculateNormal
		
		Estimates the normal to the 
		best fitting plane of a set
		of points in R(constants::dims) space. 
		This normal is the eigenvector
		corresponding to the least
		eigenvalue of the covariance
		matrix. Basically, PCA.
*/

alglib::real_1d_array calculateNormal(const alglib::real_2d_array& points, alglib::ae_int_t k)
{
	// will store normal estimation to return
	alglib::real_1d_array normal;
	
	// pcaInfo returns 1 if valid
	alglib::ae_int_t pcaInfo;
	
	// pcaS2 -> eigenvalues in descending order
	// pcaV  -> eigenvectors in corresponding order
	alglib::real_1d_array pcaS2;
	alglib::real_2d_array pcaV;

	// perform full pca on the points
	alglib::pcabuildbasis(points, k, constants::dims, pcaInfo, pcaS2, pcaV);
	
	// set normal to last eigenvector
	normal.setcontent(constants::dims, pcaV[constants::dims-1]);

	return normal;
}






int main()
{
    // load a point cloud from an obj file

	std::string filename = getCloudPointFilename();
	tinyobj::attrib_t pcloud;
	bool loaded = loadCloud(pcloud, filename);
	
	if (!loaded) {
		std::cerr << "ERROR: The OBJ file could not be loaded!" << std::endl;
		return 1;
	}

	// cast data points to double and init real_2d_array for alglib kdtree

	const size_t nPoints = pcloud.vertices.size() / constants::dims;
	alglib::real_2d_array points;
	adaptDataPoints(pcloud.vertices, nPoints, points);

	// build kdtree

	alglib::kdtree kdt;
	buildKDTree(kdt, points, nPoints);
	
	// estimate planes

	alglib::real_2d_array normals;   // indexed normals
	normals.setlength(nPoints, constants::dims);
	alglib::real_2d_array centroids; // indexed centroids
	centroids.setlength(nPoints, constants::dims);
	alglib::integer_1d_array tagsCentroids;
	tagsCentroids.setlength(nPoints);

	double kRadius = 4.0;	// this should be a function of 
							// the density and noise of the point cloud

	// for every point
	for (size_t i = 0; i < nPoints; i++) 
	{
		// generate the query point array
		alglib::real_1d_array queryPoint;
		queryPoint.setcontent(constants::dims, points[i]);

		// get the neighbors of the point for a given radius
		alglib::real_2d_array neighbors;
		alglib::ae_int_t k;
		k = getKNeighbors(kdt, neighbors, queryPoint, kRadius);

		// calculate centroid
		alglib::real_1d_array centroid = calculateCentroid(neighbors, k);
		for (size_t d = 0; d < constants::dims; d++)
			centroids[i][d] = centroid.getcontent()[d];

		// keep centroids tag indexes to retrieve normals later
		tagsCentroids[i] = i;
		
		// calculate normal
		alglib::real_1d_array normal = calculateNormal(neighbors, k);
		for (size_t d = 0; d < constants::dims; d++)
			normals[i][d] = normal.getcontent()[d];

#ifdef VERY_VERBOSE
		std::cout << "POINT " << i << " : " << std::endl;
		std::cout << "For query point " << queryPoint.tostring(constants::psd) << " with kRadius " << kRadius << std::endl;
		std::cout << "The neighborhood is " << neighbors.tostring(constants::psd) << std::endl;
		std::cout << "The centroid is " << centroid.tostring(constants::psd) << std::endl;
		std::cout << "And the normal is " << normal.tostring(constants::psd) << std::endl;
		std::cout << "With tag index " << tagsCentroids[i] << std::endl << std::endl;
#endif // VERY_VERBOSE

	}

	// now that we have the planes, use another kdtree to build a plane centroids tree
	// this kdtree will be tagged, to hold references to the normals by including the
	// centroid index, which in turn is the index of the point that it was derived from

	// build tagged kdtree of centroids

	alglib::kdtree kdtCentroids;
	buildTaggedKDTree(kdtCentroids, centroids, tagsCentroids, nPoints);

	// for every centroid
	for (size_t i = 0; i < nPoints; i++)
	{
		// generate the query centroid
		alglib::real_1d_array queryCentroid;
		queryCentroid.setcontent(constants::dims, centroids[i]);

		// get the neighbors of the centroid for a given radius
		alglib::real_2d_array neighbors;
		alglib::integer_1d_array tags;
		alglib::ae_int_t k;
		k = getKNeighborsTagged(kdtCentroids, neighbors, tags, queryCentroid, kRadius);

		// build a graph with the neighbors
#ifdef VERY_VERBOSE
		std::cout << "CENTROID " << i << " : " << std::endl;
		std::cout << "For centroid point " << queryCentroid.tostring(constants::psd) << " with kRadius " << kRadius << std::endl;
		std::cout << "The neighborhood is " << neighbors.tostring(constants::psd) << std::endl;
		std::cout << "The neighborhood tags are " << tags.tostring() << std::endl << std::endl;
#endif // VERY_VERBOSE
		
		// generate matrix representation of the graph
		double **graph = new double*[k];
		for (size_t u = 0; u < k; u++)
		{
			graph[u] = new double[k];

			for (size_t v = 0; v < k; v++)
			{
				// weight is the inverse of the normals dot product
				double weight = DBL_MAX;
				
				if (u != v)
				{
					weight = 1;
					weight -= abs(normals[tags[u]][0] * normals[tags[v]][0]);
					weight -= abs(normals[tags[u]][1] * normals[tags[v]][1]);
					weight -= abs(normals[tags[u]][2] * normals[tags[v]][2]);
				}
				
				std::cout << weight << std::endl;

				graph[u][v] = weight;

				std::cout << "w(" << tags[u] << "," << tags[v] << ") = " << graph[u][v] << std::endl << std::endl;
			}
		}
	}


	return 0;
}