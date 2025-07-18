    Info << "Reading field p\n" << endl;
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

    Info << "Reading field U\n" << endl;
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


#   include "createPhi.H"

#   include "createFields_H_gammadot.H"

    label pRefCell = 0;
    scalar pRefValue = 0.0;
    setRefCell(p, simple.dict(), pRefCell, pRefValue);
    
#ifdef OPENFOAMESIORFOUNDATION
    mesh.setFluxRequired(p.name());
#else
    mesh.schemesDict().setFluxRequired(p.name());
#endif
    
    singlePhaseTransportModel laminarTransport(U, phi);
    //	  replaced by
    //	singlePhaseTransportModel fluid(U, phi);


autoPtr<incompressible::turbulenceModel> turbulence
    (
     incompressible::turbulenceModel::New(U, phi, laminarTransport)
    );

#ifdef OPENFOAMESIORFOUNDATION
    #include "createMRF.H"
    #include "createFvOptions.H"
#endif
