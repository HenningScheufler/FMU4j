package no.ntnu.ais.fmu4j.modeldescription.fmi2;

import no.ntnu.ais.fmu4j.modeldescription.fmi1.TestVesselFmu;
import org.junit.jupiter.api.Test;

import java.io.File;
import java.io.IOException;

public class TestTrajectoryController {

    @Test
    public void test() throws IOException {
        File xmlFile = new File(TestVesselFmu.class.getClassLoader()
                .getResource("fmi2.0/TrajectoryController/modelDescription.xml").getFile());

        Fmi2ModelDescription md = Fmi2ModelDescription.fromXml(xmlFile);
    }

}
