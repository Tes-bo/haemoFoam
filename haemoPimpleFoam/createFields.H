#ifdef OPENFOAMESIORFOUNDATION
    #include "createRDeltaT.H"
#endif

    Info<< "Reading field p\n" << endl;
    volScalarField p
        (
         IOobject
         (
          "p",
          runTime.timeName(),
          mesh,
          IOobject::MUST_READ,
          IOobject::AUTO_WRITE
         ),
         mesh
        );

    Info<< "Reading field U\n" << endl;
    volVectorField U
        (
         IOobject
         (
          "U",
          runTime.timeName(),
          mesh,
          IOobject::MUST_READ,
          IOobject::AUTO_WRITE
         ),
         mesh
        );

    #include "createPhi.H"

    // Create fields for haematocrit transport model

    #include "createFields_H_gammadot.H"

    // Back to original code

    label pRefCell = 0;
    scalar pRefValue = 0.0;
    setRefCell(p, pimple.dict(), pRefCell, pRefValue);
    
#ifdef OPENFOAMESIORFOUNDATION
    mesh.setFluxRequired(p.name());
#else
    mesh.schemesDict().setFluxRequired(p.name());
#endif
    
    singlePhaseTransportModel laminarTransport(U, phi);

    autoPtr<incompressible::turbulenceModel> turbulence
        (
         incompressible::turbulenceModel::New(U, phi, laminarTransport)
        );

#ifdef OPENFOAMESIORFOUNDATION
    #include "createMRF.H"
    #include "createFvOptions.H"
#else
    Info<< "Reading field aU if present\n" << endl;
    volScalarField aU
        (
         IOobject
         (
          "aU",
          runTime.timeName(),
          mesh,
          IOobject::READ_IF_PRESENT,
          IOobject::AUTO_WRITE
         ),
         mesh,
         1/runTime.deltaT(),
         zeroGradientFvPatchScalarField::typeName
        );
#endif
