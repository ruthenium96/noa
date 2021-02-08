/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2021, Roland Grinis, GrinisRIT ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "ghmc/pms/physics.hh"
#include "ghmc/utils.hh"

#include <algorithm>
#include <regex>
#include <unordered_map>
#include <vector>

#include <pugixml.hpp>

namespace ghmc::pms::mdf
{

    using namespace ghmc::utils;

    using MDFFilePath = Path;
    using DEDXFolderPath = Path;
    using DEDXFilePath = Path;

    using ElementName = std::string;
    using MaterialName = std::string;
    using MaterialComponents =
        std::unordered_map<ElementName, ComponentFraction>;
    using Material =
        std::tuple<DEDXFilePath, MaterialDensity, MaterialComponents>;

    using CompositeName = std::string;
    using Composite = std::unordered_map<MaterialName, ComponentFraction>;

    using Elements = std::unordered_map<ElementName, AtomicElement>;
    using Materials = std::unordered_map<MaterialName, Material>;
    using Composites = std::unordered_map<CompositeName, Composite>;

    using Settings = std::tuple<Elements, Materials, Composites>;
    using ParticleName = std::string;
    using GeneratorName = std::string;

    inline const ParticleName Muon = "Muon";
    inline const ParticleName Tau = "Tau";
    inline const GeneratorName pumas = "pumas";

    struct DEDXMaterialCoefficients
    {
        float ZoA, I, a, k, x0, x1, Cbar, delta0;
    };
    struct DEDXTable
    {
        std::vector<float> T, p, Ionization, brems, pair, photonuc, Radloss,
            dEdX, CSDArange, delta, beta;
    };
    using DEDXData = std::tuple<ParticleMass, DEDXMaterialCoefficients, DEDXTable>;
    using MaterialsDEDXData = std::unordered_map<MaterialName, DEDXData>;

    inline std::regex mass_pattern(const ParticleName &particle_name)
    {
        return std::regex{"\\s*Incident particle.*" + particle_name +
                          ".*M = [0-9.E\\+-]+ MeV"};
    }
    inline const auto zoa_pattern =
        std::regex{"\\s*Absorber with <Z/A>\\s*=\\s*[0-9.E\\+-]+"};
    inline const auto coef_pattern = std::regex{"\\s*Sternheimer coef:"};
    inline const auto table_pattern =
        std::regex{"\\s*T\\s+p\\s+Ionization\\s+brems\\s+pair\\s+photonuc\\s+"
                   "Radloss\\s+dE/dx\\s+CSDA Range\\s+delta\\s+beta"};
    inline const auto units_pattern = std::regex{
        "\\s*\\[MeV\\].*\\[MeV/c\\].*\\[MeV\\s+cm\\^2/g\\].*\\[g/cm\\^2\\]"};

    inline void print_elements(const Elements &elements)
    {
        std::cout << "Elements:\n";
        for (const auto &[name, element] : elements)
        {
            std::cout << " " << name << " <Z=" << element.Z
                      << ", A=" << element.A << ", I=" << element.I
                      << ">\n";
        }
    }

    inline void print_materials(const Materials &materials)
    {
        std::cout << "Materials:\n";
        for (const auto &[name, material] : materials)
        {
            auto [file, density, comps] = material;
            std::cout << " " << name << "\n"
                      << "  dedx file: " << file << "\n"
                      << "  density: " << density << "\n"
                      << "  components:\n";
            for (const auto &[element, fraction] : comps)
                std::cout << "   " << element << ": " << fraction
                          << "\n";
        }
    }

    inline void print_dedx_header(const DEDXData &dedx_data)
    {
        std::cout << " mass=" << std::get<ParticleMass>(dedx_data) << "\n";
        auto coefs = std::get<DEDXMaterialCoefficients>(dedx_data);
        std::cout << " ZoA=" << coefs.ZoA << ", I=" << coefs.I
                  << ", a=" << coefs.a << ", k=" << coefs.k
                  << ", x0=" << coefs.x0 << ", x1=" << coefs.x1
                  << ", Cbar=" << coefs.Cbar << ", delta0=" << coefs.delta0
                  << "\n";
    }

    template <typename MDFComponents, typename Component>
    inline std::optional<MDFComponents> get_mdf_components(
        const pugi::xml_node &node,
        const std::unordered_map<std::string, Component> &comp_data,
        const std::string &tag)
    {
        auto comp_xnodes = node.select_nodes("component[@name][@fraction]");
        auto comps = MDFComponents{};
        float tot = 0.0;
        for (const auto &xnode : comp_xnodes)
        {
            auto node = xnode.node();
            auto name = node.attribute("name").value();
            auto frac = node.attribute("fraction").as_float();
            tot += frac;
            if (!comp_data.count(name))
            {
                std::cerr << "Cannot find component " << name << " for "
                          << tag << std::endl;
                return std::nullopt;
            }
            comps[name] = frac;
        }
        if (tot > 1.0)
        {
            std::cerr << "Fractions add up to " << tot << " for " << tag
                      << std::endl;
            return std::nullopt;
        }
        return comps;
    }

    inline std::optional<Settings> parse_settings(
        const GeneratorName &generated_by, const MDFFilePath &mdf_path)
    {
        auto mdf_doc = pugi::xml_document{};
        if (!mdf_doc.load_file(mdf_path.string().c_str()))
        {
            std::cerr << "Cannot load XML " << mdf_path << std::endl;
            return std::nullopt;
        }
        auto rnode = mdf_doc.child(generated_by.c_str());
        if (!rnode)
        {
            std::cerr << "MDF file not generated by " << generated_by
                      << std::endl;
            return std::nullopt;
        }

        auto element_xnodes = rnode.select_nodes("element[@name][@Z][@A][@I]");
        auto nelem = element_xnodes.size();
        if (!nelem)
        {
            std::cerr << "No atomic elements found in " << mdf_path
                      << std::endl;
            return std::nullopt;
        }
        auto elements = Elements{};
        for (const auto &xnode : element_xnodes)
        {
            auto node = xnode.node();
            elements.emplace(node.attribute("name").value(),
                             AtomicElement{node.attribute("A").as_float(),
                                           node.attribute("I").as_float(),
                                           node.attribute("Z").as_int()});
        }

        auto material_xnodes =
            rnode.select_nodes("material[@name][@file][@density]");
        auto nmtr = material_xnodes.size();
        if (!nmtr)
        {
            std::cerr << "No materials found in " << mdf_path << std::endl;
            return std::nullopt;
        }
        auto materials = Materials{};
        for (const auto &xnode : material_xnodes)
        {
            auto node = xnode.node();
            auto name = node.attribute("name").value();
            auto comps = get_mdf_components<MaterialComponents>(
                node, elements, name);
            if (!comps.has_value())
            {
                std::cerr << "Material components not consistent in "
                          << mdf_path << std::endl;
                return std::nullopt;
            }
            materials.try_emplace(name, node.attribute("file").value(),
                                  node.attribute("density").as_float(), *comps);
        }

        auto composite_xnodes = rnode.select_nodes("composite[@name]");
        auto composites = Composites{};
        for (const auto &xnode : composite_xnodes)
        {
            auto node = xnode.node();
            auto name = node.attribute("name").value();
            auto comps =
                get_mdf_components<Composite>(node, materials, name);
            if (!comps.has_value())
            {
                std::cerr << "Composite components not consistent in "
                          << mdf_path << std::endl;
                return std::nullopt;
            }
            composites.emplace(name, *comps);
        }

        return std::make_optional<Settings>(elements, materials, composites);
    }

    inline std::optional<ParticleMass> parse_particle_mass(
        std::ifstream &dedx_stream, const ParticleName &particle_name)
    {
        auto line = find_line(dedx_stream, mass_pattern(particle_name));
        if (!line.has_value())
            return std::nullopt;
        auto mass = get_numerics<float>(*line, 1);
        return (mass.has_value()) ? mass->at(0) : std::optional<ParticleMass>{};
    }

    inline std::optional<DEDXMaterialCoefficients> parse_material_coefs(
        std::ifstream &dedx_stream)
    {
        auto coefs = DEDXMaterialCoefficients{};

        auto no_data = std::sregex_iterator();

        auto line = find_line(dedx_stream, zoa_pattern);
        if (!line.has_value())
            return std::nullopt;
        auto nums = get_numerics<float>(*line, 1);
        if (!nums.has_value())
            return std::nullopt;
        coefs.ZoA = nums->at(0);

        line = find_line(dedx_stream, coef_pattern);
        if (!line.has_value())
            return std::nullopt;

        std::getline(dedx_stream, *line);
        nums = get_numerics<float>(*line, 7);
        if (!nums.has_value())
            return std::nullopt;

        coefs.a = nums->at(0);
        coefs.k = nums->at(1);
        coefs.x0 = nums->at(2);
        coefs.x1 = nums->at(3);
        coefs.I = nums->at(4);
        coefs.Cbar = nums->at(5);
        coefs.delta0 = nums->at(6);

        return coefs;
    }

    inline std::optional<DEDXTable> parse_dedx_table(std::ifstream &dedx_stream)
    {
        auto table = DEDXTable{};

        auto no_data = std::sregex_iterator();

        auto line = find_line(dedx_stream, table_pattern);
        if (!line.has_value())
            return std::nullopt;

        line = find_line(dedx_stream, units_pattern);
        if (!line.has_value())
            return std::nullopt;

        while (std::getline(dedx_stream, *line))
        {
            auto nums = get_numerics<float>(*line, 11);
            if (!nums.has_value())
                return std::nullopt;

            table.T.push_back(nums->at(0));
            table.p.push_back(nums->at(1));
            table.Ionization.push_back(nums->at(2));
            table.brems.push_back(nums->at(3));
            table.pair.push_back(nums->at(4));
            table.photonuc.push_back(nums->at(5));
            table.Radloss.push_back(nums->at(6));
            table.dEdX.push_back(nums->at(7));
            table.CSDArange.push_back(nums->at(8));
            table.delta.push_back(nums->at(9));
            table.beta.push_back(nums->at(10));
        }

        return table;
    }

    inline std::optional<DEDXData> parse_dedx_file(
        const DEDXFilePath &dedx_file_path, const ParticleName &particle_name)
    {
        if (!ghmc::utils::check_path_exists(dedx_file_path))
            return std::nullopt;

        auto dedx_stream = std::ifstream{dedx_file_path};

        auto mass = parse_particle_mass(dedx_stream, particle_name);
        if (!mass.has_value())
        {
            std::cerr << "Particle mass entry corrupted in "
                      << dedx_file_path << std::endl;
            return std::nullopt;
        }

        auto coefs = parse_material_coefs(dedx_stream);
        if (!coefs.has_value())
        {
            std::cerr << "Material coefficients data corrupted in "
                      << dedx_file_path << std::endl;
            return std::nullopt;
        }

        auto table = parse_dedx_table(dedx_stream);
        if (!table.has_value())
        {
            std::cerr << "DEDX Table corrupted in " << dedx_file_path
                      << std::endl;
            return std::nullopt;
        }

        return std::make_optional<DEDXData>(*mass, *coefs, *table);
    }

    inline std::optional<MaterialsDEDXData> parse_materials(
        const Materials &materials, const DEDXFolderPath &dedx,
        const ParticleName &particle_name)
    {
        auto materials_data = MaterialsDEDXData{};
        for (const auto &[name, material] : materials)
        {
            auto dedx_data = parse_dedx_file(
                dedx / std::get<DEDXFilePath>(material), particle_name);
            if (!dedx_data.has_value())
                return std::nullopt;
            materials_data.emplace(name, *dedx_data);
        }
        return materials_data;
    }

    inline bool check_ZoA(
        const Settings &mdf_settings, const MaterialsDEDXData &dedx_data)
    {
        auto elements = std::get<Elements>(mdf_settings);
        auto materials = std::get<Materials>(mdf_settings);
        for (const auto &[material, data] : dedx_data)
        {
            auto coefs = std::get<DEDXMaterialCoefficients>(data);
            auto ZoA = 0.f;
            for (const auto &[elmt, frac] :
                 std::get<MaterialComponents>(materials.at(material)))
                ZoA += frac * elements.at(elmt).Z / elements.at(elmt).A;
            if ((std::abs(ZoA - coefs.ZoA) > ghmc::utils::TOLERANCE))
                return false;
        }
        return true;
    }

} // namespace ghmc::pms::mdf