#pragma once

// STL headers
#include <filesystem>
#include <optional>

// TNL headers
#include <noa/3rdparty/tnl-noa/src/TNL/Algorithms/ParallelFor.h>
#include <noa/3rdparty/tnl-noa/src/TNL/Meshes/DefaultConfig.h>
#include <noa/3rdparty/tnl-noa/src/TNL/Meshes/Mesh.h>
#include <noa/3rdparty/tnl-noa/src/TNL/Meshes/MeshBuilder.h>
#include <noa/3rdparty/tnl-noa/src/TNL/Meshes/TypeResolver/resolveMeshType.h>
#include <noa/3rdparty/tnl-noa/src/TNL/Meshes/Writers/VTUWriter.h>

// Local headers
#include "ConfigTagPermissive.hh"
#include "LayerManager.hh"

namespace noa::MHFE::Storage {

// struct Domain
// `Domain` is the class that stores a TNL mesh and data over its elements
// in one place. It's more handy to use in solvers

template <typename CellTopology, typename Device = TNL::Devices::Host, typename Real = float, typename GlobalIndex = long int, typename LocalIndex = short int>
struct Domain {
	/* ----- PUBLIC TYPE ALIASES ----- */
	using MeshConfig	= TNL::Meshes::DefaultConfig<CellTopology, CellTopology::dimension, Real, GlobalIndex, LocalIndex>;
	using MeshType		= TNL::Meshes::Mesh<MeshConfig>; // Meshes only seem to be implemented on Host
	using MeshWriter	= TNL::Meshes::Writers::VTUWriter<MeshType>;
	using LayerManagerType	= LayerManager<Device, GlobalIndex>;

	using RealType		= Real;
	using GlobalIndexType	= GlobalIndex;
	using LocalIndexType	= LocalIndex;

	// Get mesh dimensions
	static constexpr int getMeshDimension() { return MeshType::getMeshDimension(); }

	protected:
	/* ----- PROTECTED DATA MEMBERS ----- */
	std::optional<MeshType> mesh = std::nullopt;	// The mesh itself
	std::vector<LayerManagerType> layers;		// Mesh data layers

	/* ----- PROTECTED METHODS ----- */
	// Updates all layer sizes from mesh entities count
	template <int fromDimension = getMeshDimension()>
	void updateLayerSizes() {
		const auto size = isClean() ? 0 : mesh.value().template getEntitiesCount<fromDimension>();
		layers.at(fromDimension).setSize(size);
		if constexpr (fromDimension > 0) updateLayerSizes<fromDimension - 1>();
	}

	public:
	/* ----- PUBLIC METHODS ----- */
	// Constructor
	Domain() {
		// If created by-itself, generate layers for each of meshes dimensions
		layers = std::vector<LayerManagerType>(getMeshDimension() + 1);
	}

	// Clear mesh data
	void clear() {
		if (isClean()) return; // Nothing to clear

		// Reset the mesh
		mesh.reset();

		// Remove all the layers
		clearLayers();
	}

	// Clears all layers
	void clearLayers() { for (auto& layer : layers) layer.clear(); }

	// Check if the domain is empty
	bool isClean() const { return mesh == std::nullopt; }

	// Get contant mesh reference
	const MeshType& getMesh() const { return mesh.value(); }

	// Get layers
	LayerManagerType& getLayers(const std::size_t& dimension) {
		return layers.at(dimension);
	}
	const LayerManagerType& getLayers(const std::size_t& dimension) const {
		return layers.at(dimension);
	}

	// Mesh loader
	void loadFrom(const std::filesystem::path& filename) {
		assert(("Mesh data is not empty, cannot load!", isClean()));

		if (!std::filesystem::exists(filename))
			throw std::runtime_error("Mesh file not found: " + filename.string() + "!");

		auto loader = [&] (auto& reader, auto&& loadedMesh) {
			if (typeid(loadedMesh) != typeid(MeshType))
				throw std::runtime_error("Read mesh has a type that differs from expected!");

			mesh = *(MeshType*)&loadedMesh;
			updateLayerSizes();

			// TODO: Find a way to get data layers through the `reader`

			return true;
		};

		using ConfigTag = ConfigTagPermissive<CellTopology>;
		if (!TNL::Meshes::resolveAndLoadMesh<ConfigTag, TNL::Devices::Host>(loader, filename, "auto"))
			throw std::runtime_error("Could not load mesh (resolveAndLoadMesh returned `false`)!");
	}

	// Mesh writer
	void write(const std::filesystem::path& filename) {
		assert(("Mesh data is empty, nothing to save!", !isClean()));

		std::ofstream file(filename);
		if (!file.is_open())
			throw std::runtime_error("Cannot open file " + filename.string() + "!");

		MeshWriter writer(file);
		writer.template writeEntities<getMeshDimension()>(mesh.value());

		// Write layers
		for (int dim = 0; dim <= getMeshDimension(); ++dim)
			for (int i = 0; i < layers.at(dim).count(); ++i) {
				if (dim == getMeshDimension())
					layers.at(dim).getLayer(i).writeCellData(writer, "cell_layer_" + std::to_string(i));
				else if (dim == 0)
					layers.at(dim).getLayer(i).writePointData(writer, "point_layer_" + std::to_string(i));
				else
					layers.at(dim).getLayer(i).writeDataArray(writer, "dim" + std::to_string(dim) + "_layer_" + std::to_string(i));
			}
	}

	/* ----- DOMAIN MESH GENERATORS ----- */
	// Declare as friends since they're supposed to modify mesh
	template <typename Device2, typename Real2, typename GlobalIndex2, typename LocalIndex2>
	friend void generate2DGrid(Domain<TNL::Meshes::Topologies::Triangle, Device2, Real2, GlobalIndex2, LocalIndex2>& domain,
				const int& Nx,
				const int& Ny,
				const float& dx,
				const float& dy);
}; // <-- struct Domain

template <typename CellTopology2, typename Device2, typename Real2, typename GlobalIndex2, typename LocalIndex2>
void generate2DGrid(Domain<CellTopology2, Device2, Real2, GlobalIndex2, LocalIndex2>& domain,
			const int& Nx,
			const int& Ny,
			const float& dx,
			const float& dy) {
	throw std::runtime_error("generate2DGrid is not implemented for this topology!");
}
template <typename Device2, typename Real2, typename GlobalIndex2, typename LocalIndex2>
void generate2DGrid(Domain<TNL::Meshes::Topologies::Triangle, Device2, Real2, GlobalIndex2, LocalIndex2>& domain,
			const int& Nx,
			const int& Ny,
			const float& dx,
			const float& dy) {
	assert(("Mesh data is not empty, cannot create grid!", domain.isClean()));

	using CellTopology = TNL::Meshes::Topologies::Triangle;
	using DomainType = Domain<CellTopology, Device2, Real2, GlobalIndex2, LocalIndex2>;
	using MeshType = typename DomainType::MeshType;

	domain.mesh = MeshType();

	using Builder = TNL::Meshes::MeshBuilder<MeshType>;
	Builder builder;

	const auto elems = Nx * Ny * 2;
	const auto points = (Nx + 1) * (Ny + 1);
	builder.setEntitiesCount(points, elems);

	using PointType = typename MeshType::MeshTraitsType::PointType;
	const auto pointId = [&] (const int& ix, const int& iy) -> GlobalIndex2 {
		return ix + (Nx + 1) * iy;
	};
	const auto fillPoints = [&] (const int& ix, const int& iy) {
		builder.setPoint(pointId(ix, iy), PointType(ix * dx, iy * dy));
	};
	TNL::Algorithms::ParallelFor2D<TNL::Devices::Host>::exec(0, 0, Nx + 1, Ny + 1, fillPoints);

	const auto fillElems = [&] (const int& ix, const int& iy, const int& u) {
		const auto cell = 2 * (ix + Nx * iy) + u;
		auto seed = builder.getCellSeed(cell);

		switch (u) {
			case 1:
				seed.setCornerId(0, pointId(ix, iy));
				seed.setCornerId(1, pointId(ix, iy + 1));
				seed.setCornerId(2, pointId(ix + 1, iy));
				break;
			case 0:
				seed.setCornerId(0, pointId(ix + 1, iy + 1));
				seed.setCornerId(1, pointId(ix + 1, iy));
				seed.setCornerId(2, pointId(ix, iy + 1));
				break;
		}
	};
	TNL::Algorithms::ParallelFor3D<TNL::Devices::Host>::exec(0, 0, 0, Nx, Ny, 2, fillElems);

	builder.build(domain.mesh.value());

	domain.updateLayerSizes();
}

} // <-- namespace noa::MHFE::Storage