/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2011-2016 OpenFOAM Foundation
     \\/     M anipulation  | Copyright (C) 2018 OpenCFD Ltd.
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "meanVelocityForceF1.H"
#include "fvMatrices.H"
#include "DimensionedField.H"
#include "IFstream.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * Static Member Functions * * * * * * * * * * * * //

namespace Foam
{
namespace fv
{
    defineTypeNameAndDebug(meanVelocityForceF1, 0);

    addToRunTimeSelectionTable
    (
        option,
        meanVelocityForceF1,
        dictionary
    );
}
}


// * * * * * * * * * * * * Protected Member Functions  * * * * * * * * * * * //

void Foam::fv::meanVelocityForceF1::writeProps
(
    const scalar gradP
) const
{
    // Only write on output time
    if (mesh_.time().writeTime())
    {
        IOdictionary propsDict
        (
            IOobject
            (
                name_ + "Properties",
                mesh_.time().timeName(),
                "uniform",
                mesh_,
                IOobject::NO_READ,
                IOobject::NO_WRITE
            )
        );
        propsDict.add("gradient", gradP);
        propsDict.regIOobject::write();
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fv::meanVelocityForceF1::meanVelocityForceF1
(
    const word& sourceName,
    const word& modelType,
    const dictionary& dict,
    const fvMesh& mesh
)
:
    cellSetOption(sourceName, modelType, dict, mesh),
    Ubar_(nullptr),
    gradP0_(0.0),
    dGradP_(0.0),
    flowDir_(0.0, 0.0, 0.0),
    //flowDir_(nullptr),
    relaxation_(coeffs_.lookupOrDefault<scalar>("relaxation", 1.0)),
    rAPtr_(nullptr)
{
    Ubar_.reset(Function1<vector>::New("Ubar", coeffs_).ptr());
    flowDir_ = Ubar_->value(0)/mag(Ubar_->value(0));

    coeffs_.readEntry("fields", fieldNames_);

    if (fieldNames_.size() != 1)
    {
        FatalErrorInFunction
            << "settings are:" << fieldNames_ << exit(FatalError);
    }

    applied_.setSize(fieldNames_.size(), false);

    // Read the initial pressure gradient from file if it exists
    IFstream propsFile
    (
        mesh_.time().timePath()/"uniform"/(name_ + "Properties")
    );

    if (propsFile.good())
    {
        Info<< "    Reading pressure gradient from file" << endl;
        dictionary propsDict(dictionary::null, propsFile);
        propsDict.readEntry("gradient", gradP0_);
    }

    Info<< "    Initial pressure gradient = " << gradP0_ << nl << endl;
}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

Foam::scalar Foam::fv::meanVelocityForceF1::magUbarAve
(
    const volVectorField& U
) const
{
    scalar magUbarAve = 0.0;

    const scalarField& cv = mesh_.V();
    forAll(cells_, i)
    {
        label celli = cells_[i];
        scalar volCell = cv[celli];
        magUbarAve += (flowDir_ & U[celli])*volCell;
    }

    reduce(magUbarAve, sumOp<scalar>());

    magUbarAve /= V_;

    return magUbarAve;
}


void Foam::fv::meanVelocityForceF1::correct(volVectorField& U)
{
    const scalarField& rAU = rAPtr_();

    // Integrate flow variables over cell set
    scalar rAUave = 0.0;
    const scalarField& cv = mesh_.V();
    forAll(cells_, i)
    {
        label celli = cells_[i];
        scalar volCell = cv[celli];
        rAUave += rAU[celli]*volCell;
    }

    // Collect across all processors
    reduce(rAUave, sumOp<scalar>());

    // Volume averages
    rAUave /= V_;

    scalar magUbarAve = this->magUbarAve(U);

    // Calculate the pressure gradient increment needed to adjust the average
    // flow-rate to the desired value
    const scalar t = mesh_.time().value();
    dGradP_ = relaxation_*(mag(Ubar_->value(t)) - magUbarAve)/rAUave;

    Ubar_.reset(Function1<vector>::New("Ubar", coeffs_).ptr());
    flowDir_ = Ubar_->value(t)/mag(Ubar_->value(t));

    // Apply correction to velocity field
    forAll(cells_, i)
    {
        label celli = cells_[i];
        U[celli] += flowDir_*rAU[celli]*dGradP_;
    }

    U.correctBoundaryConditions();

    scalar gradP = gradP0_ + dGradP_;

    Info<< "Pressure gradient source: uncorrected Ubar = " << magUbarAve
        << ", pressure gradient = " << gradP << endl;

    writeProps(gradP);
}


void Foam::fv::meanVelocityForceF1::addSup
(
    fvMatrix<vector>& eqn,
    const label fieldi
)
{
    volVectorField::Internal Su
    (
        IOobject
        (
            name_ + fieldNames_[fieldi] + "Sup",
            mesh_.time().timeName(),
            mesh_,
            IOobject::NO_READ,
            IOobject::NO_WRITE
        ),
        mesh_,
        dimensionedVector(eqn.dimensions()/dimVolume, Zero)
    );

    scalar gradP = gradP0_ + dGradP_;

    UIndirectList<vector>(Su, cells_) = flowDir_*gradP;

    eqn += Su;
}


void Foam::fv::meanVelocityForceF1::addSup
(
    const volScalarField& rho,
    fvMatrix<vector>& eqn,
    const label fieldi
)
{
    this->addSup(eqn, fieldi);
}


void Foam::fv::meanVelocityForceF1::constrain
(
    fvMatrix<vector>& eqn,
    const label
)
{
    if (rAPtr_.empty())
    {
        rAPtr_.reset
        (
            new volScalarField
            (
                IOobject
                (
                    name_ + ":rA",
                    mesh_.time().timeName(),
                    mesh_,
                    IOobject::NO_READ,
                    IOobject::NO_WRITE
                ),
                1.0/eqn.A()
            )
        );
    }
    else
    {
        rAPtr_() = 1.0/eqn.A();
    }

    gradP0_ += dGradP_;
    dGradP_ = 0.0;
}


bool Foam::fv::meanVelocityForceF1::read(const dictionary& dict)
{
    NotImplemented;

    return false;
}


// ************************************************************************* //
