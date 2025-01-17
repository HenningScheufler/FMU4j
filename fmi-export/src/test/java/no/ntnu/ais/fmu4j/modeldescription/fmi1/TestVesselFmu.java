package no.ntnu.ais.fmu4j.modeldescription.fmi1;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.io.IOException;

public class TestVesselFmu {

    @Test
    public void testVesselFmu() throws IOException {

        File xmlFile = new File(TestVesselFmu.class.getClassLoader()
                .getResource("fmi1.0/VesselFmu/modelDescription.xml").getFile());

        FmiModelDescription md = FmiModelDescription.fromXml(xmlFile);

        Assertions.assertNotNull(md.getImplementation().getCoSimulationStandAlone().capabilities);

    }

}
