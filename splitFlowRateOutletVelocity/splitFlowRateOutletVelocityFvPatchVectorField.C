/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | www.openfoam.com
     \\/     M anipulation  |
-------------------------------------------------------------------------------
    Copyright (C) 2017 OpenFOAM Foundation
    Copyright (C) 2020-2021 OpenCFD Ltd.
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

#include "splitFlowRateOutletVelocityFvPatchVectorField.H"
#include "volFields.H"
#include "one.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::splitFlowRateOutletVelocityFvPatchVectorField::
splitFlowRateOutletVelocityFvPatchVectorField
(
    const fvPatch& p,
    const DimensionedField<vector, volMesh>& iF
)
:
    fixedValueFvPatchField<vector>(p, iF),
    inletPatchName_(),
    flowSplit_(1.0),
    volumetric_(false),
    rhoName_("rho")
{}


Foam::splitFlowRateOutletVelocityFvPatchVectorField::
splitFlowRateOutletVelocityFvPatchVectorField
(
    const fvPatch& p,
    const DimensionedField<vector, volMesh>& iF,
    const dictionary& dict
)
:
    fixedValueFvPatchField<vector>(p, iF, dict, false),
    inletPatchName_(dict.get<word>("inletPatch")),
    flowSplit_(dict.getOrDefault("flowSplit", 1.0)),
    volumetric_(dict.getOrDefault("volumetric", true))
{
    if (volumetric_)
    {
        rhoName_ = "none";
    }
    else
    {
        rhoName_ = dict.getOrDefault<word>("rho", "rho");
    }

    // Value field require if mass based
    if (dict.found("value"))
    {
        fvPatchField<vector>::operator=
        (
            vectorField("value", dict, p.size())
        );
    }
    else
    {
        evaluate(Pstream::commsTypes::blocking);
    }
}


Foam::splitFlowRateOutletVelocityFvPatchVectorField::
splitFlowRateOutletVelocityFvPatchVectorField
(
    const splitFlowRateOutletVelocityFvPatchVectorField& ptf,
    const fvPatch& p,
    const DimensionedField<vector, volMesh>& iF,
    const fvPatchFieldMapper& mapper
)
:
    fixedValueFvPatchField<vector>(ptf, p, iF, mapper),
    inletPatchName_(ptf.inletPatchName_),
    flowSplit_(ptf.flowSplit_),
    volumetric_(ptf.volumetric_),
    rhoName_(ptf.rhoName_)
{}


Foam::splitFlowRateOutletVelocityFvPatchVectorField::
splitFlowRateOutletVelocityFvPatchVectorField
(
    const splitFlowRateOutletVelocityFvPatchVectorField& ptf
)
:
    fixedValueFvPatchField<vector>(ptf),
    inletPatchName_(ptf.inletPatchName_),
    flowSplit_(ptf.flowSplit_),
    volumetric_(ptf.volumetric_),
    rhoName_(ptf.rhoName_)
{}


Foam::splitFlowRateOutletVelocityFvPatchVectorField::
splitFlowRateOutletVelocityFvPatchVectorField
(
    const splitFlowRateOutletVelocityFvPatchVectorField& ptf,
    const DimensionedField<vector, volMesh>& iF
)
:
    fixedValueFvPatchField<vector>(ptf, iF),
    inletPatchName_(ptf.inletPatchName_),
    flowSplit_(ptf.flowSplit_),
    volumetric_(ptf.volumetric_),
    rhoName_(ptf.rhoName_)
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

template<class RhoType>
void Foam::splitFlowRateOutletVelocityFvPatchVectorField::updateValues
(
    const label inletPatchID,
    const RhoType& rhoOutlet,
    const RhoType& rhoInlet
)
{
    const fvPatch& p = patch();
    const fvPatch& inletPatch = p.boundaryMesh()[inletPatchID];

    const vectorField n(p.nf());

    // Extrapolate patch velocity
    vectorField Up(patchInternalField());

    // Patch normal extrapolated velocity
    scalarField nUp(n & Up);

    // Remove the normal component of the extrapolate patch velocity
    Up -= nUp*n;

    // Remove any reverse flow
    nUp = max(nUp, scalar(0));

    // Lookup non-const access to velocity field
    volVectorField& U
    (
        const_cast<volVectorField&>
        (
            dynamic_cast<const volVectorField&>(internalField())
        )
    );

    // Get the corresponding inlet velocity patch field
    fvPatchVectorField& inletPatchU = U.boundaryFieldRef()[inletPatchID];

    // Ensure that the corresponding inlet velocity patch field is up-to-date
    inletPatchU.updateCoeffs();

    // Calculate the inlet patch flow rate and apply split
    const scalar flowRate = -gSum(rhoInlet*(inletPatch.Sf() & inletPatchU))*flowSplit_;
    Info<< "Inlet flow rate: " << flowRate/flowSplit_ << endl;

    // Calculate the extrapolated outlet patch flow rate
    const scalar estimatedFlowRate = gSum(rhoOutlet*(patch().magSf()*nUp));
    Info<< "Estimated outlet flow rate " << flowRate << ", Flow Split: " << flowSplit_ << endl;

    if (estimatedFlowRate > 0.5*flowRate)
    {
        nUp *= (mag(flowRate)/mag(estimatedFlowRate));
    }
    else
    {
        nUp += ((flowRate - estimatedFlowRate)/gSum(rhoOutlet*patch().magSf()));
    }

    // Add the corrected normal component of velocity to the patch velocity
    Up += nUp*n;

    // Correct the patch velocity
    operator==(Up);
}


void Foam::splitFlowRateOutletVelocityFvPatchVectorField::updateCoeffs()
{
    if (updated())
    {
        return;
    }

    // Find corresponding inlet patch
    const label inletPatchID =
        patch().patch().boundaryMesh().findPatchID(inletPatchName_);

    if (inletPatchID < 0)
    {
        FatalErrorInFunction
            << "Unable to find inlet patch " << inletPatchName_
            << exit(FatalError);
    }

    if (volumetric_)
    {
        updateValues(inletPatchID, one{}, one{});
    }
    else
    {
        // Mass flow-rate
        if (db().foundObject<volScalarField>(rhoName_))
        {
            const volScalarField& rho = db().lookupObject<volScalarField>
            (
                rhoName_
            );

            updateValues
            (
                inletPatchID,
                rho.boundaryField()[patch().index()],
                rho.boundaryField()[inletPatchID]
            );
        }
        else
        {
            FatalErrorInFunction
                << "Cannot find density field " << rhoName_ << exit(FatalError);
        }
    }

    fixedValueFvPatchVectorField::updateCoeffs();
}


void Foam::splitFlowRateOutletVelocityFvPatchVectorField::write
(
    Ostream& os
) const
{
    fvPatchField<vector>::write(os);
    os.writeEntry("inletPatch", inletPatchName_);
    os.writeEntry("flowSplit", flowSplit_);
    if (!volumetric_)
    {
        os.writeEntry("volumetric", volumetric_);
        os.writeEntryIfDifferent<word>("rho", "rho", rhoName_);
    }
    writeEntry("value", os);
}


// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //

namespace Foam
{
   makePatchTypeField
   (
       fvPatchVectorField,
       splitFlowRateOutletVelocityFvPatchVectorField
   );
}


// ************************************************************************* //
