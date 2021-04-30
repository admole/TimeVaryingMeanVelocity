# TimeVaryingMeanVelocity

OpenFoam fvOption to introduce a source term that forces the bulk velocity towards a time varying velocity defined using function1.
Based on the OpenFOAM fvoption MeanVelocityForce ($FOAM_SRC/fvOptions/sources/derived/meanVelocityForce).

In OpenFOAM case Directory include in system/fvOptions:

```bash
momentumSource
{
    type            meanVelocityForceF1;

    selectionMode   all;

    fields          (U);
    Ubar           sine;
    UbarCoeffs
    {
        frequency   0.5;
        amplitude   0.2;
        scale       (1 0 0);
        level       (1 0 0); //offset
        t0          0;
    }
}
```
